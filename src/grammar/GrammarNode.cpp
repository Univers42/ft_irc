#include "grammar/GrammarNode.hpp"

#include <ostream>

namespace Abnf {
const int GrammarNode::kUnbounded = -1;
const int GrammarNode::kNoCapture = -1;

GrammarNode::GrammarNode() : kind(Sequence), lo(0), hi(0), first(0), count(0), literal(-1), capture(kNoCapture) {}

GrammarNode::GrammarNode(const GrammarNode& other)
    : kind(other.kind),
      lo(other.lo),
      hi(other.hi),
      first(other.first),
      count(other.count),
      literal(other.literal),
      capture(other.capture) {}

GrammarNode& GrammarNode::operator=(const GrammarNode& other) {
  if (this != &other) {
    kind = other.kind;
    lo = other.lo;
    hi = other.hi;
    first = other.first;
    count = other.count;
    literal = other.literal;
    capture = other.capture;
  }
  return *this;
}

GrammarNode::~GrammarNode() {}

std::ostream& operator<<(std::ostream& os, const GrammarNode& node) {
  static const char* const kKindNames[] = {"Reference", "Literal",     "OctetRange",
                                           "Sequence",  "Alternation", "Repetition"};
  os << kKindNames[node.kind];
  switch (node.kind) {
    case GrammarNode::Reference:
      os << "(rule=" << node.first << ")";
      break;
    case GrammarNode::Literal:
      os << "(lit=" << node.literal << ")";
      break;
    case GrammarNode::OctetRange:
      os << "(" << node.lo << "-" << node.hi << ")";
      break;
    case GrammarNode::Repetition:
      os << "(" << node.lo << "*";
      if (node.hi != GrammarNode::kUnbounded) os << node.hi;
      os << " child=" << node.first << ")";
      break;
    default:
      os << "(first=" << node.first << ", count=" << node.count << ")";
      break;
  }
  if (node.capture != GrammarNode::kNoCapture) os << " capture=" << node.capture;
  return os;
}

}  // namespace Abnf
