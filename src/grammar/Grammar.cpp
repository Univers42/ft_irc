#include "grammar/Grammar.hpp"

#include <ostream>
#include <string>

/*
** Pure storage plus accessors. There is no logic here worth the name -- the
** interesting decision was made in the header: nodes address each other by
** int index into six parallel vectors, never by pointer.
**
** That is why this file is so dull, and that is the point. Copying a Grammar
** is six vector copies; destroying one is six vector destructors; and a stale
** index is an out-of-range read rather than a dangling pointer.
**
** Note the asymmetry in the accessors, which is deliberate:
**   ruleRoot/ruleName/captureName  RANGE-CHECKED, return a sentinel. They take
**                                  indices that may come from user input
**                                  (ruleIndex() of a name that does not exist).
**   node/child/literal             UNCHECKED. Their indices only ever come
**                                  from inside the AST, so a bad one is a
**                                  builder bug and should crash, not silently
**                                  return something plausible.
*/
namespace Abnf {
const int Grammar::kNoRule = -1;

namespace {
//< Duplicated from AbnfChars rather than included: Grammar is the module's
//< data type and deliberately depends on nothing but GrammarNode, so it stays
//< usable without dragging the parser's headers along.
char asciiLower(char c) {
  if (c >= 'A' && c <= 'Z') return static_cast<char>(c - 'A' + 'a');
  return c;
}

std::string lowered(const std::string& s) {
  std::string out(s);
  for (std::string::size_type i = 0; i < out.size(); ++i) out[i] = asciiLower(out[i]);
  return out;
}

//< One shared empty string, so out-of-range accessors can return a reference
//< to something real instead of dangling or forcing callers to check first.
const std::string& emptyString() {
  static const std::string kEmpty;
  return kEmpty;
}

}  // namespace

Grammar::Grammar() {}

Grammar::Grammar(const Grammar& other)
    : _nodes(other._nodes),
      _children(other._children),
      _literals(other._literals),
      _captureNames(other._captureNames),
      _ruleNames(other._ruleNames),
      _ruleRoots(other._ruleRoots) {}

Grammar& Grammar::operator=(const Grammar& other) {
  if (this != &other) {
    _nodes = other._nodes;
    _children = other._children;
    _literals = other._literals;
    _captureNames = other._captureNames;
    _ruleNames = other._ruleNames;
    _ruleRoots = other._ruleRoots;
  }
  return *this;
}

Grammar::~Grammar() {}

//< Linear scan, and that is fine: this runs a few dozen times at startup while
//< Server binds each command to its rule, and never once per message.
int Grammar::ruleIndex(const std::string& name) const {
  const std::string key = lowered(name);  //< rule names fold, capture names do not
  for (std::size_t i = 0; i < _ruleNames.size(); ++i)
    if (_ruleNames[i] == key) return static_cast<int>(i);
  return kNoRule;
}

int Grammar::ruleRoot(int rule) const {
  if (rule < 0 || static_cast<std::size_t>(rule) >= _ruleRoots.size()) return kNoRule;
  return _ruleRoots[static_cast<std::size_t>(rule)];
}

const std::string& Grammar::ruleName(int rule) const {
  if (rule < 0 || static_cast<std::size_t>(rule) >= _ruleNames.size()) return emptyString();
  return _ruleNames[static_cast<std::size_t>(rule)];
}

std::size_t Grammar::ruleCount() const { return _ruleNames.size(); }

//< Unchecked on purpose; see the file comment. Valid indices come only from
//< ruleRoot(), child(), or a node's own `first` field.
const GrammarNode& Grammar::node(int index) const { return _nodes[static_cast<std::size_t>(index)]; }

int Grammar::child(int index) const { return _children[static_cast<std::size_t>(index)]; }

const std::string& Grammar::literal(int index) const { return _literals[static_cast<std::size_t>(index)]; }

const std::string& Grammar::captureName(int index) const {
  if (index < 0 || static_cast<std::size_t>(index) >= _captureNames.size()) return emptyString();
  return _captureNames[static_cast<std::size_t>(index)];
}

std::size_t Grammar::captureCount() const { return _captureNames.size(); }

//< Case-SENSITIVE, unlike ruleIndex(). '$' captures are this module's own
//< extension, so RFC 5234's folding rule does not apply to them.
int Grammar::captureIndex(const std::string& name) const {
  for (std::size_t i = 0; i < _captureNames.size(); ++i)
    if (_captureNames[i] == name) return static_cast<int>(i);
  return -1;
}

bool Grammar::isEmpty() const { return _ruleNames.empty(); }

void Grammar::clear() {
  _nodes.clear();
  _children.clear();
  _literals.clear();
  _captureNames.clear();
  _ruleNames.clear();
  _ruleRoots.clear();
}

//< Summary only. Dumping the whole node arena would bury the startup log; use
//< operator<<(GrammarNode) on individual nodes when actually debugging a rule.
std::ostream& operator<<(std::ostream& os, const Grammar& grammar) {
  os << "Grammar{rules=" << grammar.ruleCount() << ", captures=" << grammar.captureCount() << "}";
  return os;
}

}  // namespace Abnf
