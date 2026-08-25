#include "grammar/GrammarValidator.hpp"

#include <string>
#include <vector>

#include "grammar/GrammarNode.hpp"

/*
** Two checks, run once at the tail of GrammarBuilder::compile(). Both catch
** grammars that PARSE fine but would misbehave at match time:
**
**   checkAllRulesDefined()   a rule referenced but never given a body. The
**                            builder interns it on first reference and leaves
**                            its root at kNoRule; this is where that is noticed.
**   checkNoLeftRecursion()   a rule that can reach ITSELF without first
**                            consuming an octet. "a = a b" would make
**                            TreeMatcher recurse until its depth budget blew.
**
** The interesting one is the second, and it needs a helper. "Left position"
** is not just "the first child": in `a = b c`, rule c is ALSO in left position
** if b can match the empty string. So collectLeftReachable() has to ask
** isNullable() about each child before deciding whether to look at the next.
**
** Both walks guard against cycles by answering conservatively. isNullable()
** returns false for a rule already on its stack; that can only make the
** validator ACCEPT a grammar it might have rejected, never reject a good one,
** which is the right direction for a startup check to err in.
*/
namespace Abnf {
GrammarValidator::GrammarValidator() : _grammar(NULL) {}

GrammarValidator::~GrammarValidator() {}

const std::string& GrammarValidator::error() const { return _error; }

//< Stops at the first failure rather than collecting every problem: a grammar
//< is a startup input, so the first thing wrong with it is the thing to fix.
bool GrammarValidator::validate(const Grammar& grammar) {
  _error.clear();
  _grammar = &grammar;  //< borrowed for this call only

  if (!checkAllRulesDefined()) return false;
  if (!checkNoLeftRecursion()) return false;
  return true;
}

bool GrammarValidator::checkAllRulesDefined() {
  for (std::size_t i = 0; i < _grammar->ruleCount(); ++i) {
    if (_grammar->ruleRoot(static_cast<int>(i)) ==
        Grammar::kNoRule) {  //< "a = b" with no b · referenced, never defined
      _error = "rule '" + _grammar->ruleName(static_cast<int>(i)) + "' is referenced but never defined";
      return false;
    }
  }
  return true;
}

//< "Can this subtree succeed while consuming NOTHING?" Needed by
//< collectLeftReachable() to decide whether a sequence's later children are
//< still in left position.
bool GrammarValidator::isNullable(int node, std::vector<char>& busy) const {
  const GrammarNode& n = _grammar->node(node);

  switch (n.kind) {
    case GrammarNode::Literal:
      return _grammar->literal(n.literal).empty();

    case GrammarNode::OctetRange:
      return false;

    case GrammarNode::Reference: {
      const std::size_t rule = static_cast<std::size_t>(n.lo);
      if (busy[rule]) return false;  //< already on the stack · conservative, breaks the cycle
      busy[rule] = 1;
      const int root = _grammar->ruleRoot(n.lo);
      const bool result = (root == Grammar::kNoRule) || isNullable(root, busy);
      busy[rule] = 0;
      return result;
    }

    case GrammarNode::Sequence:  //< nullable only if EVERY child is
      for (int k = 0; k < n.count; ++k)
        if (!isNullable(_grammar->child(n.first + k), busy)) return false;
      return true;

    case GrammarNode::Alternation:  //< nullable if ANY branch is
      for (int k = 0; k < n.count; ++k)
        if (isNullable(_grammar->child(n.first + k), busy)) return true;
      return false;

    case GrammarNode::Repetition:  //< a 0-minimum can always take no iterations
      return n.lo == 0 || isNullable(_grammar->child(n.first), busy);
  }
  return false;
}

//< Marks in `seen` every rule reachable from `node` WITHOUT consuming an octet.
//< If a rule turns up in its own left-reachable set, it is left-recursive.
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
      //< Walk children only while they stay nullable. The first child that
      //< must consume something ends left position for everything after it.
      for (int k = 0; k < n.count; ++k) {
        const int c = _grammar->child(n.first + k);
        collectLeftReachable(c, seen, busy);

        if (!isNullable(c, busy)) break;
      }
      return;

    case GrammarNode::Alternation:  //< every branch starts at the same position
      for (int k = 0; k < n.count; ++k) collectLeftReachable(_grammar->child(n.first + k), seen, busy);
      return;

    case GrammarNode::Repetition:
      collectLeftReachable(_grammar->child(n.first), seen, busy);
      return;
  }
}

//< One fresh left-reachable set per rule. O(rules x nodes) and run once at
//< startup, so recomputing rather than caching costs nothing that matters.
bool GrammarValidator::checkNoLeftRecursion() {
  const std::size_t rules = _grammar->ruleCount();

  for (std::size_t r = 0; r < rules; ++r) {
    const int root = _grammar->ruleRoot(static_cast<int>(r));
    if (root == Grammar::kNoRule) continue;

    std::vector<char> seen(rules, 0);
    std::vector<char> busy(rules, 0);
    collectLeftReachable(root, seen, busy);

    if (seen[r]) {  //< rule reaches ITSELF with no input eaten · "a = a b" would spin forever
      _error = "rule '" + _grammar->ruleName(static_cast<int>(r)) + "' is left-recursive";
      return false;
    }
  }
  return true;
}

}  // namespace Abnf
