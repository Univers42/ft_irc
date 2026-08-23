#include "grammar/GrammarNode.hpp"

namespace Abnf {
const int GrammarNode::kUnbounded = -1;
const int GrammarNode::kNoCapture = -1;

GrammarNode::GrammarNode() : kind(Sequence), lo(0), hi(0), first(0), count(0), literal(-1), capture(kNoCapture) {}

}  // namespace Abnf
