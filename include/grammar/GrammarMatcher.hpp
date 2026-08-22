#ifndef GRAMMARMATCHER_HPP
#define GRAMMARMATCHER_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "grammar/Grammar.hpp"
#include "grammar/MatchResult.hpp"

/* Matches a line against a rule of a compiled Grammar.
**
** Recursive descent with backtracking. Two properties make it safe to put on a
** per-line hot path:
**
**   No allocation in the walk. Continuations are stack frames chained by
**   pointer; the only heap traffic is the capture strings on a path that
**   succeeds.
**
**   Hard bounds. Alternation over repetition can backtrack exponentially on an
**   adversarial line, and the subject grades a hang as zero -- so the walk
**   carries a step budget and a depth cap, and a line exhausting either is
**   reported as simply not matching. That is real behaviour, not a formality.
**
** The matcher holds a reference to its Grammar and does not own it. A Grammar
** must outlive every matcher built on it.
*/
class GrammarMatcher {
 public:
  /* Node visits per line, and recursion depth. Generous against the longest
  ** legal 512-octet line; finite against everything else. */
  static const long kMaxSteps;
  static const int kMaxDepth;

  explicit GrammarMatcher(const Grammar& grammar);

  /* True when `line` satisfies `rule` in FULL. A partial match is a non-match:
  ** a command line with trailing garbage is not that command. */
  bool match(int rule, const std::string& line, MatchResult& out) const;

  /* Whether the last match() gave up against a budget rather than genuinely
  ** failing. Diagnostics and tests only -- callers treat both as "no". */
  bool lastExhausted() const;

  const Grammar& grammar() const;

 private:
  GrammarMatcher(const GrammarMatcher& other);
  GrammarMatcher& operator=(const GrammarMatcher& other);

  /* Nested because they are this algorithm's private shape, not types anyone
  ** else constructs or names. */
  enum ContinuationKind {
    ContNode,
    ContSequence,
    ContRepeat,
    ContCloseCapture
  };

  struct Continuation {
    ContinuationKind kind;
    int node;
    int counter;       /* Sequence: next child. Repeat: iterations so far.
                       ** CloseCapture: capture slot. */
    std::size_t start; /* Repeat: where this iteration began.
                       ** CloseCapture: where the captured span began. */
    const Continuation* next;
  };

  struct Walk {
    const std::string* line;
    std::vector<std::string> values;
    std::vector<char> present;
    long steps;
    int depth;
    bool exhausted;
  };

  bool matchNode(int node, std::size_t pos, const Continuation* next,
                 Walk& walk) const;
  bool matchContinuation(const Continuation* k, std::size_t pos,
                         Walk& walk) const;
  bool matchSequence(int node, int childNo, std::size_t pos,
                     const Continuation* next, Walk& walk) const;
  bool matchRepetition(int node, int count, std::size_t iterStart,
                       std::size_t pos, const Continuation* next,
                       Walk& walk) const;

  /* A node that consumes exactly one octet on every match and records no
  ** capture -- which is what `middle` and `trailing` are. A run of them is
  ** counted in a loop instead of recursed once per character.
  **
  ** This is not an optimisation, it is a correctness fix: recursing per
  ** character made depth grow with LINE LENGTH, so every legal 510-octet
  ** message blew kMaxDepth and was rejected. It also happens to be four times
  ** faster. */
  bool isSingleOctet(int node) const;
  bool octetMatches(int node, unsigned char c) const;
  const unsigned char* octetBitmap(int node) const;

  const Grammar& _grammar;
  mutable bool _exhausted;

  /* 0 = not computed, 1 = yes, 2 = no. */
  mutable std::vector<char> _singleOctet;
  /* 32 bytes of membership bitmap per node, built on first use. */
  mutable std::vector<unsigned char> _bitmaps;
  mutable std::vector<char> _bitmapBuilt;
};

#endif /* GRAMMARMATCHER_HPP */
