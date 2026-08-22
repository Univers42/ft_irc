#ifndef GRAMMARNODE_HPP
#define GRAMMARNODE_HPP

namespace Abnf {
struct GrammarNode {
  enum Kind {
    Reference,
    Literal,
    OctetRange,
    Sequence,
    Alternation,
    Repetition
  };

  static const int kUnbounded;

  GrammarNode();

  Kind kind;
  int lo;
  int hi;
  int first;
  int count;
  int literal;

  int capture;

  static const int kNoCapture;
};

}  // namespace Abnf

#endif
