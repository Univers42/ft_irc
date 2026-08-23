#ifndef GRAMMARNODE_HPP
#define GRAMMARNODE_HPP

#include <iosfwd>

namespace Abnf {
struct GrammarNode {
  enum Kind { Reference, Literal, OctetRange, Sequence, Alternation, Repetition };

  static const int kUnbounded;

  GrammarNode();
  GrammarNode(const GrammarNode& other);
  GrammarNode& operator=(const GrammarNode& other);
  ~GrammarNode();

  Kind kind;
  int lo;
  int hi;
  int first;
  int count;
  int literal;

  int capture;

  static const int kNoCapture;
};

std::ostream& operator<<(std::ostream& os, const GrammarNode& node);

}  // namespace Abnf

#endif
