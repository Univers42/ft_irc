#include "grammar/GrammarNode.hpp"

#include <ostream>

namespace Abnf {
const int GrammarNode::kUnbounded = -1;
const int GrammarNode::kNoCapture = -1;

/*
** GrammarNode is a tagged union done the C++98 way: one `kind` tag plus a
** fixed set of int fields whose meaning depends on it. Reading the fields
** without first checking `kind` is meaningless.
**
**   kind          which of the six node types this is
**   lo / hi       OctetRange: the byte range, %x41-5A -> lo=0x41 hi=0x5A
**                 Repetition: min / max count, *14 -> lo=0 hi=14,
**                             hi == kUnbounded (-1) meaning "no maximum"
**                 Reference:  `lo` is the RULE INDEX. `first` is unused here.
**   first/count   start index into Grammar::_children plus how many; used by
**                 Sequence, Alternation and Repetition (which always has 1)
**   literal       index into Grammar::_literals (the "JOIN" of a command)
**   capture       index into Grammar::_captureNames, or kNoCapture (-1);
**                 set only where the source wrote '$' before a rule name
**
** There are no pointers and nothing owned, so the copy ctor and operator= are
** plain field copies and a whole Grammar is just six vectors deep-copying.
*/
//< Default is an EMPTY Sequence (count == 0), which matches nothing but is
//< harmless: GrammarBuilder always overwrites kind before adding a node.
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

//< Debug dump. Prints only the fields the node's own kind actually uses --
//< printing all seven would be noise, since most are stale for any given kind.
std::ostream& operator<<(std::ostream& os, const GrammarNode& node) {
  //< Index-parallel to enum Kind; a reorder there must be mirrored here.
  static const char* const kKindNames[] = {"Reference", "Literal",     "OctetRange",
                                           "Sequence",  "Alternation", "Repetition"};
  os << kKindNames[node.kind];
  switch (node.kind) {
    case GrammarNode::Reference:
      os << "(rule=" << node.lo << ")";  //< `lo`, NOT `first` -- see parseElement()
      break;
    case GrammarNode::Literal:
      os << "(lit=" << node.literal << ")";
      break;
    case GrammarNode::OctetRange:
      os << "(" << node.lo << "-" << node.hi << ")";
      break;
    case GrammarNode::Repetition:
      os << "(" << node.lo << "*";  //< ABNF's own spelling: "0*14", "1*", "3"
      if (node.hi != GrammarNode::kUnbounded) os << node.hi;
      os << " child=" << node.first << ")";
      break;
    default:  //< Sequence and Alternation: both are just a run of children
      os << "(first=" << node.first << ", count=" << node.count << ")";
      break;
  }
  if (node.capture != GrammarNode::kNoCapture) os << " capture=" << node.capture;
  return os;
}

}  // namespace Abnf
