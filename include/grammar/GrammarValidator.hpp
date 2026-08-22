#ifndef GRAMMARVALIDATOR_HPP
#define GRAMMARVALIDATOR_HPP

#include <string>
#include <vector>

#include "grammar/Grammar.hpp"

/* Checks that a freshly built Grammar is safe to match against.
**
** Both gates catch things that would otherwise fail late and obscurely: an
** undefined rule as a silently unmatchable line, left recursion as a hang. A
** grammar is validated once, at startup, so a failure here is a server that
** refuses to boot with a message naming the rule -- which is the correct
** outcome, and far better than discovering it mid-session.
*/
class GrammarValidator {
 public:
  GrammarValidator();

  bool validate(const Grammar& grammar);

  const std::string& error() const;

 private:
  bool checkAllRulesDefined();
  bool checkNoLeftRecursion();

  /* Can this node match the empty string? Needed by the left-recursion walk:
  ** a nullable leading element still leaves the rule at its own left edge. */
  bool isNullable(int node, std::vector<char>& busy) const;

  /* Every rule reachable at the LEFT edge of a node -- reachable with no input
  ** consumed first. A rule that reaches itself that way is left-recursive and
  ** would spin a recursive-descent matcher forever. */
  void collectLeftReachable(int node, std::vector<char>& seen,
                            std::vector<char>& busy) const;

  const Grammar* _grammar;
  std::string _error;
};

#endif /* GRAMMARVALIDATOR_HPP */
