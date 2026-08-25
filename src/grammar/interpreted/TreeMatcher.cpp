#include "grammar/interpreted/TreeMatcher.hpp"

#include <string>
#include <vector>

/*
** The interpreted strategy: walk the GrammarNode tree directly, backtracking.
** No intermediate representation, no compile step.
**
** ---- Continuations, and why they are needed ----
**
** The obvious recursive matcher ("match this node, then match the rest")
** cannot be written directly, because "the rest" is not a node. It is whatever
** the callers further up still have left to do. So the remaining work is made
** EXPLICIT: a linked list of Continuation frames, threaded through the calls.
** Each method takes the node it is on plus a `next` pointer to everything
** still owed, and calls matchContinuation() once its own part is done.
**
** `next == NULL` is the accept test -- and it demands that the cursor be at the
** END of the line, which is what makes a rule matching a mere prefix fail.
**
** Every frame is a LOCAL in the function that pushed it. Nothing is heap
** allocated, nothing outlives its creator, and unwinding is free -- which is
** exactly why a raw `const Continuation*` is safe here.
**
** ---- The budgets ----
**
** Backtracking is exponential in the worst case; *( "x" / "xx" ) against a run
** of x's is the classic. A grammar is a config file and a line comes off a
** socket, so neither is fully trusted. Two hard caps bound any single match:
**
**   kMaxSteps  total node visits -- the real defence against blow-up
**   kMaxDepth  nesting depth -- defence against smashing the C++ stack
**
** Blowing either sets Walk::exhausted, which unwinds the whole match. That is
** a THIRD answer, distinct from "no": the matcher did not decide, it gave up.
** Callers tell them apart with lastExhausted(). Note how nearly every function
** below re-checks walk.exhausted on entry and after each recursive call -- that
** is the unwind, done without exceptions.
**
** ---- The single-octet fast path ----
**
** Almost every IRC parameter is "a run of bytes from some class" -- `middle`
** and `trailing` both are. Recursing once per octet through that would burn
** the step budget on a long PRIVMSG. So when a repetition's body can only
** match one octet, matchRepetition() switches to a loop: build a 256-bit
** bitmap once, scan forward as far as it goes, then give ground one octet at a
** time if the continuation needs it. Greedy first, minimal last.
*/
namespace Abnf {
namespace Interpreted {
//< Node visits per match. Generous for any real IRC line; the fast path keeps
//< long parameters from touching it at all.
const long TreeMatcher::kMaxSteps = 200000;
//< Nesting depth, NOT line length -- long lines go through the fast path
//< without recursing. This bounds how deeply the grammar itself nests.
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

//< Does this node accept exactly this octet? Only ever asked of nodes that
//< isSingleOctet() approved, and only to populate octetBitmap()'s cache.
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

//< Same question, and the same memo-as-cycle-guard trick, as
//< ProgramCompiler::isSingleOctet(): write 2 ("no") on entry so a recursive
//< rule answers no rather than looping. A CAPTURED reference answers no too --
//< the fast path consumes octets in bulk and would skip the capture entirely.
bool TreeMatcher::isSingleOctet(int node) const {
  const std::size_t index = static_cast<std::size_t>(node);
  if (_singleOctet.size() < index + 1) _singleOctet.resize(index + 1, 0);

  if (_singleOctet[index] != 0) return _singleOctet[index] == 1;  //< memo hit · 1=yes 2=no · 2 breaks cycles

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

//< Built once per node by probing octetMatches() over all 256 values, then
//< cached forever. This is what turns the fast path's inner loop into two
//< shifts and a mask instead of a recursive descent per byte.
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

//< Resume whatever is still owed. Every match path ends up here eventually.
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
      //< The capture's body has matched, so [k->start, pos) is its text.
      //< Record it, recurse -- and if the TAIL then fails, roll the record back.
      //< Without that rollback a backtracked branch would leave phantom
      //< captures behind for a later, successful branch to report.
      const std::size_t slot = static_cast<std::size_t>(k->counter);
      std::vector<std::string>& list = walk.values[slot];
      const std::size_t mark = list.size();
      const std::size_t order = walk.sequence.size();

      const std::string text = walk.line->substr(k->start, pos - k->start);
      list.push_back(text);
      walk.sequence.push_back(text);
      walk.owners.push_back(static_cast<int>(slot));

      if (matchContinuation(k->next, pos, walk)) return true;

      list.resize(mark);  //< undo · `mark`/`order` were taken before the push
      walk.sequence.resize(order);
      walk.owners.resize(order);
      return false;
    }
  }
  return false;
}

//< Match child `childNo` onward. Pushes a frame naming the NEXT child, so the
//< sequence resumes itself once this child AND its own tail have matched --
//< that is how "then the rest" gets expressed without a return stack.
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

//< Two paths: the bulk fast path when the body is a single octet and no
//< iterations have been taken yet, otherwise one-iteration-at-a-time recursion.
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

    //< Charge the whole run at once. It is still bounded work, so it must cost
    //< budget -- but one charge, not one per octet.
    walk.steps += static_cast<long>(taken);
    if (walk.steps > kMaxSteps) {
      walk.exhausted = true;
      return false;
    }
    if (taken < least) return false;  //< 1*23(key) with 0 octets · "MODE #c +k" with an empty key

    //< Give ground from `taken` down to `least`: greedy first, minimal last.
    //< This is what lets "PRIVMSG #c :a b" hand the space back to the SPACE
    //< that follows, instead of `middle` swallowing the rest of the line.
    for (std::size_t take = taken + 1; take-- > least;) {
      if (matchContinuation(next, pos + take, walk)) return true;  //< greedy, then give ground 1 octet at a time
      if (walk.exhausted) return false;
      if (take == 0) break;
    }
    return false;
  }

  //< Slow path: try ONE more iteration, and if that fails, settle for what we
  //< have provided the minimum is met.
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

//< The dispatcher, and the only place either budget is charged. One case per
//< node kind; each one ends by calling matchContinuation() with the cursor
//< advanced past whatever it consumed.
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
      //< Follow the rule. If it is captured, push a ContCloseCapture frame
      //< remembering where the span STARTED; the frame closes it on the way
      //< back out, once the body has actually matched.
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
      //< Source order, first success wins -- and each branch is tried against
      //< the SAME continuation, so a branch that matches locally but dooms the
      //< tail is correctly rejected and the next one gets its turn.
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

  //< NULL continuation: nothing owed after the root, i.e. "the line must end
  //< exactly here". @see matchContinuation().
  const bool ok = matchNode(root, 0, NULL, walk);

  _exhausted = walk.exhausted;  //< survives the call, for lastExhausted()
  if (ok) {                     //< captures are adopted (swapped) only on success
    out.adopt(walk.values);
    out.adoptSequence(walk.sequence, walk.owners);
  }
  return ok;
}

const char* TreeMatcher::strategy() const { return "interpreted/tree"; }

}  // namespace Interpreted
}  // namespace Abnf
