#include "grammar/interpreted/TreeMatcher.hpp"

#include <string>
#include <vector>

namespace Abnf {
namespace Interpreted {
const long TreeMatcher::kMaxSteps = 200000;
const int TreeMatcher::kMaxDepth = 256;

namespace {
char fold(char c) {
  if (c >= 'A' && c <= 'Z') return static_cast<char>(c - 'A' + 'a');
  return c;
}

}  // namespace

TreeMatcher::TreeMatcher(const Grammar& grammar) : _grammar(grammar), _exhausted(false) {}

TreeMatcher::~TreeMatcher() {}

bool TreeMatcher::lastExhausted() const { return _exhausted; }

const Grammar& TreeMatcher::grammar() const { return _grammar; }

bool TreeMatcher::octetMatches(int node, unsigned char c) const {
  const GrammarNode& n = _grammar.node(node);

  switch (n.kind) {
    case GrammarNode::OctetRange:
      return static_cast<int>(c) >= n.lo && static_cast<int>(c) <= n.hi;

    case GrammarNode::Literal: {
      const std::string& text = _grammar.literal(n.literal);
      return text.size() == 1 && fold(text[0]) == fold(static_cast<char>(c));
    }

    case GrammarNode::Reference: {
      const int root = _grammar.ruleRoot(n.lo);
      return root != Grammar::kNoRule && octetMatches(root, c);
    }

    case GrammarNode::Alternation:
      for (int i = 0; i < n.count; ++i)
        if (octetMatches(_grammar.child(n.first + i), c)) return true;
      return false;

    default:
      return false;
  }
}

bool TreeMatcher::isSingleOctet(int node) const {
  const std::size_t index = static_cast<std::size_t>(node);
  if (_singleOctet.size() < index + 1) _singleOctet.resize(index + 1, 0);

  if (_singleOctet[index] != 0) return _singleOctet[index] == 1;  //< memo hit · 1=yes 2=no · 2 also breaks rule cycles

  _singleOctet[index] = 2;

  const GrammarNode& n = _grammar.node(node);
  bool yes = false;

  switch (n.kind) {
    case GrammarNode::OctetRange:
      yes = true;
      break;

    case GrammarNode::Literal:
      yes = (_grammar.literal(n.literal).size() == 1);
      break;

    case GrammarNode::Reference: {
      const int root = _grammar.ruleRoot(n.lo);
      yes = (n.capture == GrammarNode::kNoCapture) && root != Grammar::kNoRule && isSingleOctet(root);
      break;
    }

    case GrammarNode::Alternation: {
      yes = (n.count > 0);
      for (int i = 0; i < n.count && yes; ++i)
        if (!isSingleOctet(_grammar.child(n.first + i))) yes = false;
      break;
    }

    default:
      yes = false;
      break;
  }

  _singleOctet[index] = yes ? 1 : 2;
  return yes;
}

const unsigned char* TreeMatcher::octetBitmap(int node) const {
  const std::size_t index = static_cast<std::size_t>(node);

  if (_bitmapBuilt.size() < index + 1) {
    _bitmapBuilt.resize(index + 1, 0);
    _bitmaps.resize((index + 1) * 32, 0);
  }

  unsigned char* bits = &_bitmaps[index * 32];
  if (!_bitmapBuilt[index]) {
    for (int c = 0; c < 256; ++c)
      if (octetMatches(node, static_cast<unsigned char>(c))) bits[c >> 3] |= static_cast<unsigned char>(1u << (c & 7));
    _bitmapBuilt[index] = 1;
  }
  return bits;
}

bool TreeMatcher::matchContinuation(const Continuation* k, std::size_t pos, Walk& walk) const {
  if (walk.exhausted) return false;  //< a budget blew deeper in · unwind, do no more work

  if (k == NULL) return pos == walk.line->size();  //< WHOLE line must be consumed · "JOIN #a junk" fails here

  switch (k->kind) {
    case ContNode:
      return matchNode(k->node, pos, k->next, walk);

    case ContSequence:
      return matchSequence(k->node, k->counter, pos, k->next, walk);

    case ContRepeat:
      return matchRepetition(k->node, k->counter, k->start, pos, k->next, walk);

    case ContCloseCapture: {
      const std::size_t slot = static_cast<std::size_t>(k->counter);
      std::vector<std::string>& list = walk.values[slot];
      const std::size_t mark = list.size();
      const std::size_t order = walk.sequence.size();

      const std::string text = walk.line->substr(k->start, pos - k->start);
      list.push_back(text);
      walk.sequence.push_back(text);
      walk.owners.push_back(static_cast<int>(slot));

      if (matchContinuation(k->next, pos, walk)) return true;

      list.resize(mark);
      walk.sequence.resize(order);
      walk.owners.resize(order);
      return false;
    }
  }
  return false;
}

bool TreeMatcher::matchSequence(int node, int childNo, std::size_t pos, const Continuation* next, Walk& walk) const {
  const GrammarNode& n = _grammar.node(node);
  if (childNo >= n.count) return matchContinuation(next, pos, walk);  //< seq done · "USER" SP u SP m SP un SP ":" rn

  Continuation frame;
  frame.kind = ContSequence;
  frame.node = node;
  frame.counter = childNo + 1;
  frame.start = 0;
  frame.next = next;

  return matchNode(_grammar.child(n.first + childNo), pos, &frame, walk);
}

bool TreeMatcher::matchRepetition(int node, int count, std::size_t iterStart, std::size_t pos, const Continuation* next,
                                  Walk& walk) const {
  if (walk.exhausted) return false;

  const GrammarNode& n = _grammar.node(node);
  const int child = _grammar.child(n.first);

  if (count == 0 &&
      isSingleOctet(child)) {  //< fast path · `trailing`/`middle` eat 1 octet each -> count, don't recurse
    const std::size_t least = static_cast<std::size_t>(n.lo < 0 ? 0 : n.lo);
    const std::size_t most = (n.hi == GrammarNode::kUnbounded) ? walk.line->size() : static_cast<std::size_t>(n.hi);

    const unsigned char* bits = octetBitmap(child);
    const std::string& line = *walk.line;

    std::size_t taken = 0;
    std::size_t p = pos;
    while (taken < most && p < line.size()) {
      const unsigned char c = static_cast<unsigned char>(line[p]);
      if (!(bits[c >> 3] & (1u << (c & 7)))) break;  //< 256-bit class test · stops `middle` at the next SPACE
      ++p;
      ++taken;
    }

    walk.steps += static_cast<long>(taken);
    if (walk.steps > kMaxSteps) {
      walk.exhausted = true;
      return false;
    }
    if (taken < least) return false;  //< 1*23(key) with 0 octets · "MODE #c +k" with an empty key

    for (std::size_t take = taken + 1; take-- > least;) {
      if (matchContinuation(next, pos + take, walk)) return true;  //< greedy, then give ground 1 octet at a time
      if (walk.exhausted) return false;
      if (take == 0) break;
    }
    return false;
  }

  bool canRepeat = (n.hi == GrammarNode::kUnbounded) || (count < n.hi);

  if (count > 0 && pos == iterStart) canRepeat = false;  //< zero-width guard · *( [ "x" ] ) would spin forever

  if (canRepeat) {
    Continuation frame;
    frame.kind = ContRepeat;
    frame.node = node;
    frame.counter = count + 1;
    frame.start = pos;
    frame.next = next;

    if (matchNode(child, pos, &frame, walk)) return true;
    if (walk.exhausted) return false;
  }

  if (count >= n.lo) return matchContinuation(next, pos, walk);  //< min satisfied · *14(x) accepts 0..14
  return false;
}

bool TreeMatcher::matchNode(int node, std::size_t pos, const Continuation* next, Walk& walk) const {
  if (walk.exhausted) return false;

  if (++walk.steps > kMaxSteps) {  //< 200k node visits · *( "x" / "xx" ) on 400 x's blows this
    walk.exhausted = true;
    return false;
  }
  if (walk.depth >= kMaxDepth) {  //< 256 frames · deep nesting, not line length (that is the fast path's job)
    walk.exhausted = true;
    return false;
  }
  ++walk.depth;

  const GrammarNode& n = _grammar.node(node);
  bool ok = false;

  switch (n.kind) {
    case GrammarNode::Literal: {
      const std::string& text = _grammar.literal(n.literal);
      if (pos + text.size() <= walk.line->size()) {
        bool same = true;
        for (std::size_t i = 0; i < text.size(); ++i) {
          if (fold((*walk.line)[pos + i]) != fold(text[i])) {  //< literals are case-blind · "join"=="JOIN"
            same = false;
            break;
          }
        }
        if (same) ok = matchContinuation(next, pos + text.size(), walk);
      }
      break;
    }

    case GrammarNode::OctetRange: {
      if (pos < walk.line->size()) {
        const int c = static_cast<unsigned char>((*walk.line)[pos]);
        if (c >= n.lo && c <= n.hi) ok = matchContinuation(next, pos + 1, walk);  //< one octet · %x21-39 etc.
      }
      break;
    }

    case GrammarNode::Reference: {
      const int root = _grammar.ruleRoot(n.lo);
      if (root != Grammar::kNoRule) {
        if (n.capture != GrammarNode::kNoCapture) {
          Continuation frame;
          frame.kind = ContCloseCapture;
          frame.node = -1;
          frame.counter = n.capture;
          frame.start = pos;
          frame.next = next;
          ok = matchNode(root, pos, &frame, walk);
        } else {
          ok = matchNode(root, pos, next, walk);
        }
      }
      break;
    }

    case GrammarNode::Sequence:
      ok = matchSequence(node, 0, pos, next, walk);
      break;

    case GrammarNode::Alternation:
      for (int i = 0; i < n.count; ++i) {
        if (matchNode(_grammar.child(n.first + i), pos, next, walk)) {
          ok = true;
          break;
        }
        if (walk.exhausted) break;
      }
      break;

    case GrammarNode::Repetition:
      ok = matchRepetition(node, 0, pos, pos, next, walk);
      break;
  }

  --walk.depth;
  return ok;
}

bool TreeMatcher::match(int rule, const std::string& line, MatchResult& out) const {
  _exhausted = false;

  out.reset(_grammar);

  const int root = _grammar.ruleRoot(rule);
  if (root == Grammar::kNoRule) return false;

  Walk walk;
  walk.line = &line;
  walk.values.assign(_grammar.captureCount(), std::vector<std::string>());
  walk.sequence.clear();
  walk.owners.clear();
  walk.steps = 0;
  walk.depth = 0;
  walk.exhausted = false;

  const bool ok = matchNode(root, 0, NULL, walk);

  _exhausted = walk.exhausted;
  if (ok) {
    out.adopt(walk.values);
    out.adoptSequence(walk.sequence, walk.owners);
  }
  return ok;
}

const char* TreeMatcher::strategy() const { return "interpreted/tree"; }

}  // namespace Interpreted
}  // namespace Abnf
