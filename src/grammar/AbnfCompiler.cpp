#include "grammar/AbnfCompiler.hpp"

#include <cstdlib>
#include <sstream>

#include "grammar/AbnfChars.hpp"
#include "grammar/AbnfLineReader.hpp"
#include "grammar/GrammarValidator.hpp"

using AbnfChars::isAlpha;
using AbnfChars::isDigit;
using AbnfChars::isHexDigit;
using AbnfChars::isRuleChar;
using AbnfChars::lowered;
using AbnfChars::skipBlanks;

AbnfCompiler::AbnfCompiler() : _grammar(NULL), _lineNo(0) {}

const std::string& AbnfCompiler::error() const { return _error; }

bool AbnfCompiler::fail(const std::string& message) {
  std::ostringstream os;
  os << "grammar: line " << _lineNo << ": " << message;
  _error = os.str();
  return false;
}

/* ─── Interning and node construction ─── */

int AbnfCompiler::internRule(const std::string& name) {
  const std::string key = lowered(name);
  for (std::size_t i = 0; i < _grammar->_ruleNames.size(); ++i)
    if (_grammar->_ruleNames[i] == key) return static_cast<int>(i);

  _grammar->_ruleNames.push_back(key);
  /* A reference to a rule not yet defined leaves a placeholder root. Anything
  ** still unset when compilation ends is an undefined rule. */
  _grammar->_ruleRoots.push_back(Grammar::kNoRule);
  return static_cast<int>(_grammar->_ruleNames.size() - 1);
}

int AbnfCompiler::internCapture(const std::string& name) {
  for (std::size_t i = 0; i < _grammar->_captureNames.size(); ++i)
    if (_grammar->_captureNames[i] == name) return static_cast<int>(i);

  _grammar->_captureNames.push_back(name);
  return static_cast<int>(_grammar->_captureNames.size() - 1);
}

int AbnfCompiler::addNode(const GrammarNode& node) {
  _grammar->_nodes.push_back(node);
  return static_cast<int>(_grammar->_nodes.size() - 1);
}

int AbnfCompiler::addChildren(const std::vector<int>& children) {
  const int first = static_cast<int>(_grammar->_children.size());
  for (std::size_t i = 0; i < children.size(); ++i)
    _grammar->_children.push_back(children[i]);
  return first;
}

/* ─── num-val = "%" "x" 1*HEXDIG [ "-" 1*HEXDIG / 1*( "." 1*HEXDIG ) ] ─── */

bool AbnfCompiler::parseNumericValue(const std::string& s, std::size_t& i,
                                     int& out) {
  if (i >= s.size() || (s[i] != 'x' && s[i] != 'X'))
    return fail("only the %x form of num-val is supported");
  ++i;

  std::vector<int> pieces;
  for (;;) {
    std::string hex;
    while (i < s.size() && isHexDigit(s[i]))
      hex += s[i++];
    if (hex.empty()) return fail("%x with no hex digits");

    const int low = static_cast<int>(std::strtol(hex.c_str(), NULL, 16));
    int high = low;

    if (i < s.size() && s[i] == '-') {
      ++i;
      std::string upper;
      while (i < s.size() && isHexDigit(s[i]))
        upper += s[i++];
      if (upper.empty()) return fail("%x range with no upper bound");
      high = static_cast<int>(std::strtol(upper.c_str(), NULL, 16));
      if (high < low) return fail("%x range runs backwards");
    }

    GrammarNode range;
    range.kind = GrammarNode::OctetRange;
    range.lo = low;
    range.hi = high;
    pieces.push_back(addNode(range));

    /* %x0D.0A -- a dotted concatenation of octet values. */
    if (i < s.size() && s[i] == '.') {
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

/* ─── element = rulename / group / option / char-val / num-val ─── */

bool AbnfCompiler::parseElement(const std::string& s, std::size_t& i,
                                int& out) {
  skipBlanks(s, i);
  if (i >= s.size()) return fail("expected an element, found end of rule");

  char c = s[i];

  if (c == '(') {
    ++i;
    if (!parseAlternation(s, i, out)) return false;
    skipBlanks(s, i);
    if (i >= s.size() || s[i] != ')') return fail("unclosed '('");
    ++i;
    return true;
  }

  if (c == '[') {
    ++i;
    int inner = 0;
    if (!parseAlternation(s, i, inner)) return false;
    skipBlanks(s, i);
    if (i >= s.size() || s[i] != ']') return fail("unclosed '['");
    ++i;

    /* An option is just a 0-or-1 repetition. */
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

  if (c == '"') {
    ++i;
    std::string text;
    while (i < s.size() && s[i] != '"') text += s[i++];
    if (i >= s.size()) return fail("unterminated string literal");
    ++i;

    GrammarNode lit;
    lit.kind = GrammarNode::Literal;
    lit.literal = static_cast<int>(_grammar->_literals.size());
    _grammar->_literals.push_back(text);
    out = addNode(lit);
    return true;
  }

  if (c == '%') {
    ++i;
    return parseNumericValue(s, i, out);
  }

  /* `$name` -- the one extension: match rule `name`, record its span. */
  bool capture = false;
  if (c == '$') {
    capture = true;
    ++i;
    skipBlanks(s, i);
    if (i >= s.size() || !isAlpha(s[i]))
      return fail("'$' must be followed by a rule name");
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

/* ─── repetition = [repeat] element ─── */

bool AbnfCompiler::parseRepetition(const std::string& s, std::size_t& i,
                                   int& out) {
  skipBlanks(s, i);

  int low = 1;
  int high = 1;
  bool repeated = false;

  if (i < s.size() && (isDigit(s[i]) || s[i] == '*')) {
    repeated = true;
    std::string before;
    while (i < s.size() && isDigit(s[i])) before += s[i++];

    if (i < s.size() && s[i] == '*') {
      ++i;
      low = before.empty()
                ? 0
                : static_cast<int>(std::strtol(before.c_str(), NULL, 10));
      std::string after;
      while (i < s.size() && isDigit(s[i])) after += s[i++];
      high = after.empty()
                 ? GrammarNode::kUnbounded
                 : static_cast<int>(std::strtol(after.c_str(), NULL, 10));
    } else {
      /* `5(x)` -- an exact count. */
      low = high = static_cast<int>(std::strtol(before.c_str(), NULL, 10));
    }

    if (high != GrammarNode::kUnbounded && high < low)
      return fail("repetition range runs backwards");
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

/* ─── concatenation = repetition *(1*c-wsp repetition) ─── */

bool AbnfCompiler::parseConcatenation(const std::string& s, std::size_t& i,
                                      int& out) {
  std::vector<int> parts;

  for (;;) {
    skipBlanks(s, i);
    if (i >= s.size()) break;
    const char c = s[i];
    if (c == '/' || c == ')' || c == ']') break;

    int part = 0;
    if (!parseRepetition(s, i, part)) return false;
    parts.push_back(part);
  }

  if (parts.empty()) return fail("empty concatenation");
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

/* ─── alternation = concatenation *(*c-wsp "/" *c-wsp concatenation) ─── */

bool AbnfCompiler::parseAlternation(const std::string& s, std::size_t& i,
                                    int& out) {
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

/* ─── rule = rulename defined-as elements ─── */

bool AbnfCompiler::parseRule(const std::string& line, std::size_t lineNo) {
  _lineNo = lineNo;

  std::size_t i = 0;
  skipBlanks(line, i);
  if (i >= line.size()) return true;

  if (!isAlpha(line[i])) return fail("a rule must begin with a rule name");
  std::string name;
  while (i < line.size() && isRuleChar(line[i])) name += line[i++];

  skipBlanks(line, i);
  if (i >= line.size() || line[i] != '=')
    return fail("expected '=' or '=/' after rule name '" + name + "'");
  ++i;

  bool incremental = false;
  if (i < line.size() && line[i] == '/') {
    incremental = true;
    ++i;
  }

  const int rule = internRule(name);

  int body = 0;
  if (!parseAlternation(line, i, body)) return false;

  skipBlanks(line, i);
  if (i < line.size()) return fail("trailing text after rule '" + name + "'");

  const std::size_t slot = static_cast<std::size_t>(rule);

  if (incremental) {
    /* `=/` folds the new alternative in beside what is already there. Building
    ** a fresh Alternation of [old, new] rather than appending to an existing
    ** one keeps the child array append-only: a child list is a contiguous
    ** range, so growing one in place would mean it is no longer last. */
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

  if (_grammar->_ruleRoots[slot] != Grammar::kNoRule)
    return fail("rule '" + name + "' is defined twice (use '=/' to extend it)");

  _grammar->_ruleRoots[slot] = body;
  return true;
}

/* ─── compile ─────────────────────────────────────────────────────────────
**
** Read lines, parse each into nodes, then validate the whole graph. Each of
** those three is somebody else's class; this is just the order. */
bool AbnfCompiler::compile(const std::string& text, Grammar& out) {
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

  GrammarValidator validator;
  if (!validator.validate(out)) {
    _lineNo = 0;
    return fail(validator.error());
  }
  return true;
}
