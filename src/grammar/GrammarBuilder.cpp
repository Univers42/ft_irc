#include "grammar/GrammarBuilder.hpp"

#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

#include "grammar/AbnfChars.hpp"
#include "grammar/AbnfLineReader.hpp"
#include "grammar/GrammarValidator.hpp"

/*
** The real piece of work in the module: a recursive-descent parser that turns
** one line of ABNF into GrammarNodes.
**
** The parse* functions below are ONE FUNCTION PER PRECEDENCE LEVEL, written
** loosest-binding first. Each one consumes exactly what belongs to its level,
** calls the next level down for its operands, and loops on its own operator:
**
**   parseRule           name "=" / "=/" then the body
**     parseAlternation    branches separated by '/'        <- binds loosest
**       parseConcatenation  adjacent elements, juxtaposition
**         parseRepetition     an optional "*" prefix
**           parseElement        (group) [option] "literal" %xNN $capture name
**             parseAlternation  ...back to the top, for a group
**
** Shared contract, held by every parse* function:
**   - takes (const string& s, size_t& i, int& out)
**   - consumes from i forward, never moves it backwards
**   - on success writes a node index to `out` and returns true
**   - on failure returns false with _error already set, via fail()
**
** One structural rule worth stating once: a construct with only one part does
** NOT get a node. A single-branch alternation, a one-element concatenation and
** an unrepeated element each return their child index straight through. That
** is why the AST has no chains of one-child wrappers in it.
*/

namespace Abnf {
using AbnfChars::isAlpha;
using AbnfChars::isDigit;
using AbnfChars::isHexDigit;
using AbnfChars::isRuleChar;
using AbnfChars::lowered;
using AbnfChars::skipBlanks;

GrammarBuilder::GrammarBuilder() : _grammar(NULL), _lineNo(0) {}

GrammarBuilder::~GrammarBuilder() {}

const std::string& GrammarBuilder::error() const { return _error; }

//< Returns false so callers can write `return fail("...")` and set both the
//< error and the return value in one statement. Line 0 means "no single line
//< is to blame" -- what the validator's errors get.
bool GrammarBuilder::fail(const std::string& message) {
  std::ostringstream os;
  os << "grammar: line " << _lineNo << ": " << message;
  _error = os.str();
  return false;
}

int GrammarBuilder::internRule(const std::string& name) {
  const std::string key = lowered(name);  //< RFC 5234: rule names are case-insensitive
  for (std::size_t i = 0; i < _grammar->_ruleNames.size(); ++i)
    if (_grammar->_ruleNames[i] == key) return static_cast<int>(i);

  _grammar->_ruleNames.push_back(key);

  //< A brand-new rule starts with NO body. That is deliberate: it lets "a = b"
  //< intern b on first reference, long before b's own line is read. Any rule
  //< still sitting at kNoRule when the file ends is what the validator rejects.
  _grammar->_ruleRoots.push_back(Grammar::kNoRule);
  return static_cast<int>(_grammar->_ruleNames.size() - 1);
}

//< Note the missing lowered() compared to internRule: capture names keep their
//< case, because they are OUR extension and are looked up verbatim by handlers.
int GrammarBuilder::internCapture(const std::string& name) {
  for (std::size_t i = 0; i < _grammar->_captureNames.size(); ++i)
    if (_grammar->_captureNames[i] == name) return static_cast<int>(i);

  _grammar->_captureNames.push_back(name);
  return static_cast<int>(_grammar->_captureNames.size() - 1);
}

int GrammarBuilder::addNode(const GrammarNode& node) {
  _grammar->_nodes.push_back(node);
  return static_cast<int>(_grammar->_nodes.size() - 1);
}

//< Children are appended and never reclaimed: each parent owns one contiguous
//< slice of _children forever. Wasteful in principle, free in practice -- a
//< grammar is built once at startup and then only read.
int GrammarBuilder::addChildren(const std::vector<int>& children) {
  const int first = static_cast<int>(_grammar->_children.size());
  for (std::size_t i = 0; i < children.size(); ++i) _grammar->_children.push_back(children[i]);
  return first;
}

bool GrammarBuilder::parseNumericValue(const std::string& s, std::size_t& i, int& out) {
  if (i >= s.size() || (s[i] != 'x' && s[i] != 'X'))
    return fail("only the %x form of num-val is supported");  //< %x20 ok · %d32 %b100000 no
  ++i;

  std::vector<int> pieces;
  for (;;) {
    std::string hex;
    while (i < s.size() && isHexDigit(s[i])) hex += s[i++];
    if (hex.empty()) return fail("%x with no hex digits");  //< "%x" · "%x-39" · "%xZZ"

    const int low = static_cast<int>(std::strtol(hex.c_str(), NULL, 16));
    int high = low;

    if (i < s.size() && s[i] == '-') {  //< range form · "%x41-5A" · plain "%x20" skips this
      ++i;
      std::string upper;
      while (i < s.size() && isHexDigit(s[i])) upper += s[i++];
      if (upper.empty()) return fail("%x range with no upper bound");  //< "%x41-" trails off
      high = static_cast<int>(std::strtol(upper.c_str(), NULL, 16));
      if (high < low) return fail("%x range runs backwards");  //< "%x5A-41" · Z..A is empty
    }

    GrammarNode range;
    range.kind = GrammarNode::OctetRange;
    range.lo = low;
    range.hi = high;
    pieces.push_back(addNode(range));

    if (i < s.size() && s[i] == '.') {  //< dotted concat · "%x0D.0A" = CRLF as two octets
      ++i;
      continue;
    }
    break;
  }

  if (pieces.size() == 1) {
    out = pieces[0];
    return true;
  }

  GrammarNode seq;
  seq.kind = GrammarNode::Sequence;
  seq.first = addChildren(pieces);
  seq.count = static_cast<int>(pieces.size());
  out = addNode(seq);
  return true;
}

bool GrammarBuilder::parseElement(const std::string& s, std::size_t& i, int& out) {
  skipBlanks(s, i);
  if (i >= s.size()) return fail("expected an element, found end of rule");

  char c = s[i];

  if (c == '(') {  //< group · "( letter / special )" binds the alternation before *8
    ++i;
    if (!parseAlternation(s, i, out)) return false;
    skipBlanks(s, i);
    if (i >= s.size() || s[i] != ')') return fail("unclosed '('");  //< "( letter" runs off the line
    ++i;
    return true;
  }

  if (c == '[') {  //< option = 0-or-1 rep · "[ SPACE keylist ]" · JOIN's key list
    ++i;
    int inner = 0;
    if (!parseAlternation(s, i, inner)) return false;
    skipBlanks(s, i);
    if (i >= s.size() || s[i] != ']') return fail("unclosed '['");  //< "[ SPACE x" never closes
    ++i;

    GrammarNode rep;
    rep.kind = GrammarNode::Repetition;
    rep.lo = 0;
    rep.hi = 1;
    std::vector<int> kids;
    kids.push_back(inner);
    rep.first = addChildren(kids);
    rep.count = 1;
    out = addNode(rep);
    return true;
  }

  if (c == '"') {  //< char-val · "JOIN" matches join/JoIn (RFC 5234: case-insensitive)
    ++i;
    std::string text;
    while (i < s.size() && s[i] != '"') text += s[i++];
    if (i >= s.size()) return fail("unterminated string literal");  //< opening quote, no closer
    ++i;

    GrammarNode lit;
    lit.kind = GrammarNode::Literal;
    lit.literal = static_cast<int>(_grammar->_literals.size());
    _grammar->_literals.push_back(text);
    out = addNode(lit);
    return true;
  }

  if (c == '%') {  //< num-val · "%x01-09" in nospcrlfcl
    ++i;
    return parseNumericValue(s, i, out);
  }

  bool capture = false;
  if (c == '$') {  //< OUR extension, not RFC 5234 · "$realname" records the span it matched
    capture = true;
    ++i;
    skipBlanks(s, i);
    if (i >= s.size() || !isAlpha(s[i])) return fail("'$' must be followed by a rule name");  //< "$\"x\"" · "$ "
    c = s[i];
  }

  if (isAlpha(c)) {
    std::string name;
    while (i < s.size() && isRuleChar(s[i])) name += s[i++];

    GrammarNode ref;
    ref.kind = GrammarNode::Reference;
    ref.lo = internRule(name);
    if (capture) ref.capture = internCapture(name);
    out = addNode(ref);
    return true;
  }

  return fail(std::string("unexpected character '") + c + "' in rule body");
}

bool GrammarBuilder::parseRepetition(const std::string& s, std::size_t& i, int& out) {
  skipBlanks(s, i);

  int low = 1;
  int high = 1;
  bool repeated = false;

  if (i < s.size() && (isDigit(s[i]) || s[i] == '*')) {  //< repeat prefix · "*14(" "1*23(" "3digit" "7("
    repeated = true;
    std::string before;
    while (i < s.size() && isDigit(s[i])) before += s[i++];

    if (i < s.size() && s[i] == '*') {  //< "1*23" -> min 1 max 23 · bare "3" -> exactly 3
      ++i;
      low = before.empty() ? 0 : static_cast<int>(std::strtol(before.c_str(), NULL, 10));
      std::string after;
      while (i < s.size() && isDigit(s[i])) after += s[i++];
      high = after.empty() ? GrammarNode::kUnbounded : static_cast<int>(std::strtol(after.c_str(), NULL, 10));
    } else {
      low = high = static_cast<int>(std::strtol(before.c_str(), NULL, 10));
    }

    if (high != GrammarNode::kUnbounded && high < low) return fail("repetition range runs backwards");  //< "5*2(x)"
  }

  int child = 0;
  if (!parseElement(s, i, child)) return false;

  if (!repeated) {
    out = child;
    return true;
  }

  GrammarNode rep;
  rep.kind = GrammarNode::Repetition;
  rep.lo = low;
  rep.hi = high;
  std::vector<int> kids;
  kids.push_back(child);
  rep.first = addChildren(kids);
  rep.count = 1;
  out = addNode(rep);
  return true;
}

bool GrammarBuilder::parseConcatenation(const std::string& s, std::size_t& i, int& out) {
  std::vector<int> parts;

  for (;;) {
    skipBlanks(s, i);
    if (i >= s.size()) break;
    const char c = s[i];
    if (c == '/' || c == ')' || c == ']') break;  //< concat ends at an alt bar or an enclosing close

    int part = 0;
    if (!parseRepetition(s, i, part)) return false;
    parts.push_back(part);
  }

  if (parts.empty()) return fail("empty concatenation");  //< "a = / b" · "a = ( )"
  if (parts.size() == 1) {
    out = parts[0];
    return true;
  }

  GrammarNode seq;
  seq.kind = GrammarNode::Sequence;
  seq.first = addChildren(parts);
  seq.count = static_cast<int>(parts.size());
  out = addNode(seq);
  return true;
}

bool GrammarBuilder::parseAlternation(const std::string& s, std::size_t& i, int& out) {
  std::vector<int> branches;

  int first = 0;
  if (!parseConcatenation(s, i, first)) return false;
  branches.push_back(first);

  for (;;) {
    skipBlanks(s, i);
    if (i >= s.size() || s[i] != '/') break;
    ++i;
    int branch = 0;
    if (!parseConcatenation(s, i, branch)) return false;
    branches.push_back(branch);
  }

  if (branches.size() == 1) {
    out = branches[0];
    return true;
  }

  GrammarNode alt;
  alt.kind = GrammarNode::Alternation;
  alt.first = addChildren(branches);
  alt.count = static_cast<int>(branches.size());
  out = addNode(alt);
  return true;
}

//< One logical line -> one installed rule. AbnfLineReader has already joined
//< continuations and stripped comments, so `line` is guaranteed to be a whole
//< rule on a single string.
bool GrammarBuilder::parseRule(const std::string& line, std::size_t lineNo) {
  _lineNo = lineNo;  //< every fail() from here down blames this line

  std::size_t i = 0;
  skipBlanks(line, i);
  if (i >= line.size()) return true;  //< an all-blank line is a silent success, not an error

  if (!isAlpha(line[i])) return fail("a rule must begin with a rule name");
  std::string name;
  while (i < line.size() && isRuleChar(line[i])) name += line[i++];

  skipBlanks(line, i);
  if (i >= line.size() || line[i] != '=') return fail("expected '=' or '=/' after rule name '" + name + "'");
  ++i;

  bool incremental = false;
  if (i < line.size() && line[i] == '/') {  //< "=/" -- extend the rule rather than define it
    incremental = true;
    ++i;
  }

  const int rule = internRule(name);

  int body = 0;
  if (!parseAlternation(line, i, body)) return false;

  //< parseAlternation stops at anything it does not understand, so leftover
  //< text means a typo the parser silently walked past -- e.g. a stray ')'.
  skipBlanks(line, i);
  if (i < line.size()) return fail("trailing text after rule '" + name + "'");

  const std::size_t slot = static_cast<std::size_t>(rule);

  if (incremental) {
    //< "=/" folds the OLD root and the new body into one Alternation, which is
    //< precisely RFC 5234's definition of incremental alternation. The embedded
    //< grammar uses it once, to give `params` a second, 14-parameter form.
    if (_grammar->_ruleRoots[slot] == Grammar::kNoRule)
      return fail("'" + name + " =/' before that rule was ever defined");

    std::vector<int> kids;
    kids.push_back(_grammar->_ruleRoots[slot]);
    kids.push_back(body);

    GrammarNode alt;
    alt.kind = GrammarNode::Alternation;
    alt.first = addChildren(kids);
    alt.count = 2;
    _grammar->_ruleRoots[slot] = addNode(alt);
    return true;
  }

  //< Still kNoRule means "interned by a forward reference but never defined",
  //< which is the normal case here. Anything else is a genuine redefinition.
  if (_grammar->_ruleRoots[slot] != Grammar::kNoRule)
    return fail("rule '" + name + "' is defined twice (use '=/' to extend it)");

  _grammar->_ruleRoots[slot] = body;
  return true;
}

//< The whole pipeline for stage 2, in three phases: unfold, parse, validate.
//< `out` is cleared up front, so a failed compile leaves an empty Grammar
//< rather than a half-built one.
bool GrammarBuilder::compile(const std::string& text, Grammar& out) {
  _error.clear();
  _lineNo = 0;

  out.clear();
  _grammar = &out;

  AbnfLineReader reader;
  std::vector<AbnfLineReader::Line> lines;
  if (!reader.read(text, lines)) {
    _lineNo = reader.errorLine();
    return fail(reader.error());
  }

  for (std::size_t i = 0; i < lines.size(); ++i)
    if (!parseRule(lines[i].text, lines[i].number)) return false;

  //< Parsing succeeded, which says nothing about whether the grammar is USABLE.
  //< Undefined references and left recursion are both well-formed on the page.
  GrammarValidator validator;
  if (!validator.validate(out)) {
    _lineNo = 0;  //< a left-recursive cycle belongs to no single line
    return fail(validator.error());
  }
  return true;
}

}  // namespace Abnf
