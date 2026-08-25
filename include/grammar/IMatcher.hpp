/**
 * @file IMatcher.hpp
 * @brief The seam between the two matching strategies.
 *
 * Stage 3 of the pipeline:
 * @verbatim
 *   IGrammarSource -> AbnfLineReader -> GrammarBuilder -> Grammar -> IMatcher
 *                                                                   ^^^^^^^^
 * @endverbatim
 *
 * Two implementations answer exactly the same question -- "does this line
 * match this rule, and what did the captures grab?" -- by completely different
 * means:
 *
 * @verbatim
 *   Interpreted::TreeMatcher      walks the AST directly, backtracking
 *     + handles recursive rules, unbounded repetition, captures anywhere
 *     + no compile step, so startup is free
 *     - backtracking: needs a step and depth budget to stay bounded
 *
 *   Compiled::ProgramMatcher      compiles each rule to a bytecode program
 *                                 and runs it as a Thompson/Pike VM
 *     + linear in line length, no backtracking, no budget needed
 *     - inlines rules, so it cannot express recursion
 *     - cannot put a capture inside an unbounded repetition
 * @endverbatim
 *
 * Server::initGrammar() defaults to TreeMatcher and switches to ProgramMatcher
 * when FT_IRC_MATCHER=compiled. Because both go through this interface,
 * everything above them -- Server::parseLine(), Message, every command handler
 * -- is written once and does not know which strategy is running.
 */
#ifndef IMATCHER_HPP
#define IMATCHER_HPP

#include <string>

#include "grammar/MatchResult.hpp"

namespace Abnf {
/** @brief Abstract "match a line against a rule" service. */
class IMatcher {
 public:
  virtual ~IMatcher() {}

  /**
   * @brief Matches @p line against rule @p rule.
   * @param rule     Rule index, from Grammar::ruleIndex().
   * @param line     The line to match, WITHOUT its CRLF terminator.
   * @param[out] out Receives the captures; reset() first, so it is safe to
   *                 reuse one MatchResult across calls.
   * @return true only on a match of the WHOLE line. A rule that matches a
   *         prefix and leaves a tail is a failure, not a partial success --
   *         "JOIN #a junk" is not a valid JOIN.
   * @note @c const, and both implementations honour it: their scratch buffers
   *       are @c mutable caches, not observable state. That is what lets
   *       Server hold the matcher by const pointer and match from const code.
   */
  virtual bool match(int rule, const std::string& line, MatchResult& out) const = 0;

  /**
   * @brief @return A short name for the strategy, for startup logging:
   *         "interpreted/tree" or "compiled/pike". Never NULL.
   */
  virtual const char* strategy() const = 0;

  /**
   * @brief Did the last match() give up on a budget rather than decide?
   * @return true if it ran out of steps or depth, meaning "do not know"
   *         rather than "no".
   * @note Only TreeMatcher can ever return true here; ProgramMatcher has no
   *       budget to exhaust and always answers false. The distinction matters
   *       to a caller that wants to tell a malformed line apart from a line
   *       that was merely too expensive to decide.
   */
  virtual bool lastExhausted() const = 0;

 protected:
  /** @brief Protected so only derived classes can construct one. */
  IMatcher() {}
  /** @brief Protected copy ctor; the base subobject holds no state. */
  IMatcher(const IMatcher& other) { (void)other; }
  /** @brief Protected assignment, for symmetry with the copy ctor. */
  IMatcher& operator=(const IMatcher& other) {
    (void)other;
    return *this;
  }
};

}  // namespace Abnf

#endif
