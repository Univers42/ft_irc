/**
 * @file TreeMatcher.hpp
 * @brief The INTERPRETED strategy: walks the AST directly, with backtracking.
 *
 * The default IMatcher. Where the compiled side lowers a rule to bytecode and
 * runs every alternative at once, this one interprets the GrammarNode tree as
 * it stands, trying alternatives in source order and backing up when one fails:
 * @verbatim
 *   Grammar --TreeMatcher--> MatchResult      (no intermediate representation)
 * @endverbatim
 *
 * @section tm_cps Continuations instead of a return stack
 *
 * The obvious recursive matcher ("match this node, then the rest") cannot be
 * written directly, because "the rest" is not a node -- it is whatever the
 * callers up the chain still have left to do. So the remaining work is made
 * explicit as a linked list of Continuation frames, threaded through the
 * calls: each matcher method takes the node it is on plus a @p next pointer to
 * everything still owed, and calls matchContinuation() when its own part is
 * done. @c next == NULL means "nothing left", which is the accept test.
 *
 * The frames live on the C++ stack -- each is a local in the frame that pushed
 * it -- so there is no allocation anywhere in a match, and unwinding is free.
 * That is why they are passed as @c const Continuation*.
 *
 * @section tm_budget Why there are budgets
 *
 * Backtracking is exponential in the worst case: @c *( "x" / "xx" ) against a
 * run of x's is the classic example. A grammar is a config file here, and a
 * line comes off a socket, so neither is fully trusted. Two hard caps keep any
 * single match bounded:
 *
 *  - kMaxSteps -- total node visits, the real defence against blow-up.
 *  - kMaxDepth -- nesting depth, defence against deep recursion smashing the
 *    C++ stack. Not line length: long lines are handled by the fast path below
 *    without recursing at all.
 *
 * Hitting either sets Walk::exhausted, which unwinds the whole match and makes
 * lastExhausted() true. That is a THIRD answer, distinct from "no": the matcher
 * did not decide, it gave up.
 *
 * @section tm_fast The single-octet fast path
 *
 * Almost every IRC parameter is "a run of bytes from some class" -- @c middle
 * and @c trailing both are. Recursing once per octet through that would burn
 * the step budget on a long PRIVMSG. So when a repetition's body can only match
 * one octet, matchRepetition() switches to a loop: build a 256-bit bitmap for
 * the body once, scan forward as far as it goes, then hand back ground one
 * octet at a time if the continuation needs it. Greedy first, minimal last.
 *
 * @section tm_vs Against the compiled strategy
 *
 * What this one can do that ProgramMatcher cannot: recursive rules, unbounded
 * repetition containing captures, and no compile step at startup. What it gives
 * up: worst-case linearity. @see IMatcher for the side-by-side.
 */
#ifndef TREEMATCHER_HPP
#define TREEMATCHER_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "grammar/Grammar.hpp"
#include "grammar/IMatcher.hpp"
#include "grammar/MatchResult.hpp"

namespace Abnf {
namespace Interpreted {
/** @brief Backtracking AST interpreter; @see the file comment. */
class TreeMatcher : public IMatcher {
 public:
  /** @brief Node-visit budget for one match(); exceeding it sets exhausted. */
  static const long kMaxSteps;

  /** @brief Nesting-depth budget for one match(); guards the C++ stack. */
  static const int kMaxDepth;

  /**
   * @brief Binds to @p grammar. Nothing is precomputed.
   * @param grammar Borrowed by reference, so it must outlive the matcher.
   */
  explicit TreeMatcher(const Grammar& grammar);
  virtual ~TreeMatcher();

  /**
   * @brief Matches @p line against rule @p rule. @copydoc IMatcher::match
   * @note A false return means either "no match" or "gave up"; call
   *       lastExhausted() to tell them apart.
   */
  virtual bool match(int rule, const std::string& line, MatchResult& out) const;

  /** @brief @return The literal "interpreted/tree". */
  virtual const char* strategy() const;

  /** @brief @return true if the last match() hit a budget instead of deciding. */
  virtual bool lastExhausted() const;

  /** @brief @return The bound grammar, for callers that need to resolve names. */
  const Grammar& grammar() const;

 private:
  //< No default ctor (nothing to bind to) and non-copyable, in line with the
  //< rest of the module. Declared and never defined.
  TreeMatcher();
  TreeMatcher(const TreeMatcher& other);
  TreeMatcher& operator=(const TreeMatcher& other);

  /** @brief Which kind of pending work a Continuation frame represents. */
  enum ContinuationKind {
    ContNode,         //< Match one node, then continue with `next`.
    ContSequence,     //< Resume a Sequence at child number `counter`.
    ContRepeat,       //< Resume a Repetition after `counter` iterations.
    ContCloseCapture  //< Close the capture opened at `start`, recording the span.
  };

  /**
   * @brief One frame of "what is still owed" after the current node.
   * @note Always a local in the frame that created it. Never heap-allocated,
   *       never outlives its creator -- which is exactly why a raw @c next
   *       pointer is safe here.
   */
  struct Continuation {
    ContinuationKind kind;     //< Selects how the fields below are read.
    int node;                  //< Node to resume at; -1 for ContCloseCapture.
    int counter;               //< Child number, iteration count, or capture slot.
    std::size_t start;         //< Iteration start, or capture start offset.
    const Continuation* next;  //< The rest of the chain; NULL means done.
  };

  /**
   * @brief Per-match mutable state, threaded by reference through the walk.
   * @note Grouped into one struct so every method takes a single @c Walk&
   *       rather than half a dozen out-parameters, and so nothing per-match
   *       has to live in the matcher itself.
   */
  struct Walk {
    const std::string* line;                        //< The line being matched.
    std::vector<std::vector<std::string> > values;  //< Captures, by slot.
    std::vector<std::string> sequence;              //< Captures, in line order.
    std::vector<int> owners;                        //< Slot of each `sequence` entry.
    long steps;                                     //< Node visits so far, vs kMaxSteps.
    int depth;                                      //< Current nesting, vs kMaxDepth.
    bool exhausted;                                 //< Set once a budget blew.
  };

  /**
   * @brief Matches @p node at @p pos, then @p next. The main dispatcher.
   * @return true if node AND the whole continuation chain matched.
   * @note Charges one step and one depth level, and is where both budgets are
   *       enforced. Literal comparison here is case-insensitive, per RFC 5234.
   */
  bool matchNode(int node, std::size_t pos, const Continuation* next, Walk& walk) const;

  /**
   * @brief Resumes the pending work in @p k at @p pos.
   * @return true if everything still owed matched.
   * @note @p k == NULL is the accept test, and it demands that @p pos be the
   *       END of the line -- a rule matching only a prefix fails right here.
   * @note ContCloseCapture records its span, recurses, and UNDOES the record
   *       if the tail fails. That rollback is what keeps a backtracked branch
   *       from leaving phantom captures behind.
   */
  bool matchContinuation(const Continuation* k, std::size_t pos, Walk& walk) const;

  /**
   * @brief Matches Sequence @p node from child @p childNo onward.
   * @note Pushes a ContSequence frame naming the NEXT child, so the sequence
   *       resumes itself once the current child and its own tail have matched.
   */
  bool matchSequence(int node, int childNo, std::size_t pos, const Continuation* next, Walk& walk) const;

  /**
   * @brief Matches Repetition @p node, having already taken @p count iterations.
   * @param iterStart Offset the current iteration began at; used by the
   *                  zero-width guard.
   * @note Two paths. The fast path (@see tm_fast) applies when the body is a
   *       single octet and no iterations have been taken yet. Otherwise it
   *       recurses, trying one more iteration before settling for what it has.
   * @note An iteration that consumed nothing stops the loop. Without that
   *       guard, @c *( [ "x" ] ) would spin forever on a zero-width body.
   */
  bool matchRepetition(int node, int count, std::size_t iterStart, std::size_t pos, const Continuation* next,
                       Walk& walk) const;

  /**
   * @brief Can @p node only ever match exactly one octet?
   * @note Memoised in _singleOctet, and the memo doubles as a cycle guard --
   *       same trick as ProgramCompiler::isSingleOctet(). A captured reference
   *       answers false, because the fast path would skip its capture.
   */
  bool isSingleOctet(int node) const;

  /** @brief Does @p node accept octet @p c? Recursive; drives octetBitmap(). */
  bool octetMatches(int node, unsigned char c) const;

  /**
   * @brief @return A cached 32-byte (256-bit) bitmap of @p node's octets.
   * @note Built once per node by probing octetMatches() over all 256 values,
   *       then reused. This is what turns the fast path's inner loop into two
   *       shifts and a mask.
   */
  const unsigned char* octetBitmap(int node) const;

  const Grammar& _grammar;  //< Borrowed; outlives this matcher.
  mutable bool _exhausted;  //< Result of the last match's budget check.

  //< Lazy per-node caches. Mutable because they are caches, not state: they
  //< are derived entirely from _grammar and never change what a match returns.
  //< Same caveat as ProgramMatcher -- caching under const means not thread-safe.
  mutable std::vector<char> _singleOctet;  //< 0 unknown, 1 yes, 2 no.

  mutable std::vector<unsigned char> _bitmaps;  //< 32 bytes per node, flat.
  mutable std::vector<char> _bitmapBuilt;       //< Whether that slice is filled.
};

}  // namespace Interpreted
}  // namespace Abnf

#endif
