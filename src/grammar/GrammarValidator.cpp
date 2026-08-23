#include "grammar/GrammarValidator.hpp"

#include <string>
#include <vector>

#include "grammar/GrammarNode.hpp"

namespace Abnf {
GrammarValidator::GrammarValidator() : _grammar(NULL) {}

GrammarValidator::~GrammarValidator() {}

const std::string& GrammarValidator::error() const { return _error; }

bool GrammarValidator::validate(const Grammar& grammar) {
  _error.clear();
  _grammar = &grammar;

  if (!checkAllRulesDefined()) return false;
  if (!checkNoLeftRecursion()) return false;
  return true;
}

bool GrammarValidator::checkAllRulesDefined() {
  for (std::size_t i = 0; i < _grammar->ruleCount(); ++i) {
    if (_grammar->ruleRoot(static_cast<int>(i)) == Grammar::kNoRule) {
      _error = "rule '" + _grammar->ruleName(static_cast<int>(i)) + "' is referenced but never defined";
      return false;
    }
  }
  return true;
}

bool GrammarValidator::isNullable(int node, std::vector<char>& busy) const {
  const GrammarNode& n = _grammar->node(node);

  switch (n.kind) {
    case GrammarNode::Literal:
      return _grammar->literal(n.literal).empty();

    case GrammarNode::OctetRange:
      return false;

    case GrammarNode::Reference: {
      const std::size_t rule = static_cast<std::size_t>(n.lo);
      if (busy[rule]) return false;
      busy[rule] = 1;
      const int root = _grammar->ruleRoot(n.lo);
      const bool result = (root == Grammar::kNoRule) || isNullable(root, busy);
      busy[rule] = 0;
      return result;
    }

    case GrammarNode::Sequence:
      for (int k = 0; k < n.count; ++k)
        if (!isNullable(_grammar->child(n.first + k), busy)) return false;
      return true;

    case GrammarNode::Alternation:
      for (int k = 0; k < n.count; ++k)
        if (isNullable(_grammar->child(n.first + k), busy)) return true;
      return false;

    case GrammarNode::Repetition:
      return n.lo == 0 || isNullable(_grammar->child(n.first), busy);
  }
  return false;
}

void GrammarValidator::collectLeftReachable(int node, std::vector<char>& seen, std::vector<char>& busy) const {
  const GrammarNode& n = _grammar->node(node);

  switch (n.kind) {
    case GrammarNode::Literal:
    case GrammarNode::OctetRange:
      return;

    case GrammarNode::Reference: {
      const std::size_t rule = static_cast<std::size_t>(n.lo);
      if (seen[rule]) return;
      seen[rule] = 1;
      const int root = _grammar->ruleRoot(n.lo);
      if (root != Grammar::kNoRule) collectLeftReachable(root, seen, busy);
      return;
    }

    case GrammarNode::Sequence:
      for (int k = 0; k < n.count; ++k) {
        const int c = _grammar->child(n.first + k);
        collectLeftReachable(c, seen, busy);

        if (!isNullable(c, busy)) break;
      }
      return;

    case GrammarNode::Alternation:
      for (int k = 0; k < n.count; ++k) collectLeftReachable(_grammar->child(n.first + k), seen, busy);
      return;

    case GrammarNode::Repetition:
      collectLeftReachable(_grammar->child(n.first), seen, busy);
      return;
  }
}

bool GrammarValidator::checkNoLeftRecursion() {
  const std::size_t rules = _grammar->ruleCount();

  for (std::size_t r = 0; r < rules; ++r) {
    const int root = _grammar->ruleRoot(static_cast<int>(r));
    if (root == Grammar::kNoRule) continue;

    std::vector<char> seen(rules, 0);
    std::vector<char> busy(rules, 0);
    collectLeftReachable(root, seen, busy);

    if (seen[r]) {
      _error = "rule '" + _grammar->ruleName(static_cast<int>(r)) + "' is left-recursive";
      return false;
    }
  }
  return true;
}

}  // namespace Abnf
