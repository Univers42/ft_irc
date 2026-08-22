#ifndef GRAMMARNODE_HPP
#define GRAMMARNODE_HPP

/* One node of a compiled grammar.
**
** Nodes live in a single contiguous array owned by Grammar and refer to each
** other by index, never by pointer. That is deliberate: the matcher walks this
** structure once per inbound line, and an index-addressed array stays in cache
** where a graph of separately allocated nodes would not.
**
** The int fields are reused per kind rather than unioned, because a union of
** two ints saves nothing and costs clarity:
**
**   Reference    lo = rule index
**   Literal      literal = index into the literal table
**   OctetRange   lo = first octet, hi = last octet, both inclusive
**   Sequence     children are [first, first + count)
**   Alternation  children are [first, first + count)
**   Repetition   lo = minimum, hi = maximum or kUnbounded; one child
*/
struct GrammarNode {
  enum Kind {
    Reference,
    Literal,
    OctetRange,
    Sequence,
    Alternation,
    Repetition
  };

  /* Repetition upper bound meaning "no limit" -- ABNF `*x` and `1*x`. */
  static const int kUnbounded;

  GrammarNode();

  Kind kind;
  int lo;
  int hi;
  int first;
  int count;
  int literal;
  /* Index into the grammar's capture names, or kNoCapture. A `$name`
  ** reference records the span it matched; everything else records nothing. */
  int capture;

  static const int kNoCapture;
};

#endif /* GRAMMARNODE_HPP */
