#include "grammar/GrammarBuilder.hpp"

#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

#include "grammar/AbnfChars.hpp"
#include "grammar/AbnfLineReader.hpp"
#include "grammar/GrammarValidator.hpp"

namespace Abnf {
using AbnfChars::isAlpha;
using AbnfChars::isDigit;
using AbnfChars::isHexDigit;
using AbnfChars::isRuleChar;
using AbnfChars::lowered;
using AbnfChars::skipBlanks;

GrammarBuilder::GrammarBuilder() : _grammar(NULL), _lineNo(0) {}

const std::string& GrammarBuilder::error() const { return _error; }

bool GrammarBuilder::fail(const std::string& message) {
  std::ostringstream os;
  os << "grammar: line " << _lineNo << ": " << message;
  _error = os.str();
  return false;
}

int GrammarBuilder::internRule(const std::string& name) {
  const std::string key = lowered(name);
  for (std::size_t i = 0; i < _grammar->_ruleNames.size(); ++i)
    if (_grammar->_ruleNames[i] == key) return static_cast<int>(i);

  _grammar->_ruleNames.push_back(key);

  _grammar->_ruleRoots.push_back(Grammar::kNoRule);
  return static_cast<int>(_grammar->_ruleNames.size() - 1);
}

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

int GrammarBuilder::addChildren(const std::vector<int>& children) {
  const int first = static_cast<int>(_grammar->_children.size());
  for (std::size_t i = 0; i < children.size(); ++i)
    _grammar->_children.push_back(children[i]);
  return first;
}

bool GrammarBuilder::parseNumericValue(const std::string& s, std::size_t& i,
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

bool GrammarBuilder::parseElement(const std::string& s, std::size_t& i,
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

bool GrammarBuilder::parseRepetition(const std::string& s, std::size_t& i,
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

bool GrammarBuilder::parseConcatenation(const std::string& s, std::size_t& i,
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

bool GrammarBuilder::parseAlternation(const std::string& s, std::size_t& i,
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

bool GrammarBuilder::parseRule(const std::string& line, std::size_t lineNo) {
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

  GrammarValidator validator;
  if (!validator.validate(out)) {
    _lineNo = 0;
    return fail(validator.error());
  }
  return true;
}

}  // namespace Abnf
