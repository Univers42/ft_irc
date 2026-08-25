/**
 * @file GrammarValidator.hpp
 * @brief Post-parse sanity checks on a Grammar.
 *
 * Runs at the tail of GrammarBuilder::compile(). It catches the two mistakes
 * that parse perfectly well but would break a matcher at run time:
 *
 *  1. @b Undefined @b rule -- "a = b" where @c b is never given a body. The
 *     builder happily interns @c b on first reference and leaves its root at
 *     Grammar::kNoRule; this is where that gets noticed.
 *  2. @b Left @b recursion -- "a = a b", or any cycle reachable without first
 *     consuming an octet. TreeMatcher would recurse until its depth budget
 *     blew; better to reject the grammar at startup than to fail per message.
 *
 * @note Left recursion is checked, general recursion is not. A right-recursive
 *       rule is fine for TreeMatcher; ProgramCompiler rejects it separately,
 *       when it tries to inline it. @see ProgramCompiler
 */
#ifndef GRAMMARVALIDATOR_HPP
#define GRAMMARVALIDATOR_HPP

#include <string>
#include <vector>

#include "grammar/Grammar.hpp"

namespace Abnf {
/** @brief Read-only checker; never modifies the grammar it inspects. */
class GrammarValidator {
 public:
  GrammarValidator();
  ~GrammarValidator();

  /**
   * @brief Runs every check against @p grammar.
   * @param grammar Grammar to inspect; borrowed for the call's duration only.
   * @return true if it passes all checks; false with error() set on the first
   *         failure -- checks stop there rather than collecting every problem.
   */
  bool validate(const Grammar& grammar);

  /** @brief @return Why validation failed, or "" if it did not. */
  const std::string& error() const;

 private:
  //< Non-copyable: _grammar is a borrowed pointer, same reasoning as
  //< GrammarBuilder. Declared and never defined.
  GrammarValidator(const GrammarValidator& other);
  GrammarValidator& operator=(const GrammarValidator& other);

  /**
   * @brief Check 1: every interned rule actually has a body.
   * @return false the moment a rule's root is still Grammar::kNoRule.
   */
  bool checkAllRulesDefined();

  /**
   * @brief Check 2: no rule can reach itself without consuming input.
   * @return false with the offending rule named in error().
   * @note One left-reachable set per rule, computed from scratch. O(rules x
   *       nodes) and run once at startup, so the repeated work does not matter.
   */
  bool checkNoLeftRecursion();

  /**
   * @brief Can @p node match the empty string?
   * @param node        Node index to test.
   * @param[in,out] busy Per-rule recursion guard, sized to ruleCount().
   * @return true if @p node can succeed while consuming nothing.
   * @note Needed by collectLeftReachable(): in a Sequence, the second child is
   *       only in leftmost position if the first one could match nothing.
   * @note A rule already on the @p busy stack answers false. That is the
   *       conservative choice -- it breaks the cycle and can only make the
   *       validator accept a grammar it might have rejected, never the reverse.
   */
  bool isNullable(int node, std::vector<char>& busy) const;

  /**
   * @brief Marks every rule reachable from @p node without consuming an octet.
   * @param node        Node index to walk from.
   * @param[in,out] seen One flag per rule; set for each left-reachable rule.
   * @param[in,out] busy Scratch guard, forwarded to isNullable().
   * @note Sequence walks children only while they stay nullable; Alternation
   *       walks all of them, since every branch starts at the same position.
   */
  void collectLeftReachable(int node, std::vector<char>& seen, std::vector<char>& busy) const;

  const Grammar* _grammar;  //< Borrowed; valid only inside validate().
  std::string _error;       //< First failure found; "" means it passed.
};

}  // namespace Abnf

#endif
