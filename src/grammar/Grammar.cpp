#include "grammar/Grammar.hpp"

const int Grammar::kNoRule = -1;

namespace {

char asciiLower(char c) {
  if (c >= 'A' && c <= 'Z') return static_cast<char>(c - 'A' + 'a');
  return c;
}

std::string lowered(const std::string& s) {
  std::string out(s);
  for (std::string::size_type i = 0; i < out.size(); ++i)
    out[i] = asciiLower(out[i]);
  return out;
}

const std::string kEmpty;

}  // namespace

Grammar::Grammar() {}

int Grammar::ruleIndex(const std::string& name) const {
  const std::string key = lowered(name);
  for (std::size_t i = 0; i < _ruleNames.size(); ++i)
    if (_ruleNames[i] == key) return static_cast<int>(i);
  return kNoRule;
}

int Grammar::ruleRoot(int rule) const {
  if (rule < 0 || static_cast<std::size_t>(rule) >= _ruleRoots.size())
    return kNoRule;
  return _ruleRoots[static_cast<std::size_t>(rule)];
}

const std::string& Grammar::ruleName(int rule) const {
  if (rule < 0 || static_cast<std::size_t>(rule) >= _ruleNames.size())
    return kEmpty;
  return _ruleNames[static_cast<std::size_t>(rule)];
}

std::size_t Grammar::ruleCount() const { return _ruleNames.size(); }

const GrammarNode& Grammar::node(int index) const {
  return _nodes[static_cast<std::size_t>(index)];
}

int Grammar::child(int index) const {
  return _children[static_cast<std::size_t>(index)];
}

const std::string& Grammar::literal(int index) const {
  return _literals[static_cast<std::size_t>(index)];
}

const std::string& Grammar::captureName(int index) const {
  if (index < 0 || static_cast<std::size_t>(index) >= _captureNames.size())
    return kEmpty;
  return _captureNames[static_cast<std::size_t>(index)];
}

std::size_t Grammar::captureCount() const { return _captureNames.size(); }

bool Grammar::isEmpty() const { return _ruleNames.empty(); }

void Grammar::clear() {
  _nodes.clear();
  _children.clear();
  _literals.clear();
  _captureNames.clear();
  _ruleNames.clear();
  _ruleRoots.clear();
}
