#include "grammar/compiled/ProgramCompiler.hpp"

#include <cstring>
#include <string>
#include <vector>

#include "grammar/GrammarNode.hpp"

/*
** Lowers one Grammar rule into a flat Program. A single recursive walk,
** emitNode(), with one case per GrammarNode::Kind:
**
**   OctetRange   one Class op over the range's bits
**   Literal      one Class op per character, each holding BOTH cases
**   Sequence     its children back to back; emits no instruction of its own
**   Alternation  Split before each branch, Jump past the rest after it
**   Repetition   emitRepetition(), below
**   Reference    INLINED -- the referenced rule's body is emitted right here
**   $capture     Save 2n ... body ... Save 2n+1
**
** Branches are emitted with placeholder targets and BACKPATCHED once the real
** address is known: emit() returns the instruction's index precisely so the
** caller can reach back and fill in .x or .y afterwards. Every Split and Jump
** in this file works that way.
**
** ---- The two limitations, and why they are not bugs ----
**
** Inlining is what makes the program flat, and it is the root of both:
**
**   1. RECURSION is refused. Inlining a rule that references itself would not
**      terminate, so _compiling[] flags each rule while its body is being
**      emitted and a re-entry fails. TreeMatcher has no such limit, which is a
**      real reason it is the default strategy.
**   2. A CAPTURE UNDER AN UNBOUNDED REPETITION is refused. The loop form
**      reuses one slot pair per iteration, so *( $x ) would report only its
**      last match. A BOUNDED repetition is fine -- it unrolls, giving every
**      iteration its own slots. That is why the embedded grammar writes
**      *13( SPACE $modeparam ) with an explicit bound rather than a bare *.
**
** Both are reported at startup via ProgramMatcher::compileAll(), so a grammar
** this strategy cannot express is a refusal to boot, not a silent misparse.
*/
namespace Abnf {
namespace Compiled {
namespace {
//< Cap on how many copies a bounded repetition may unroll into. *14(x) is
//< fine; *999(x) would emit thousands of instructions, so it is an error.
const int kUnrollLimit = 64;

char fold(char c) {
  if (c >= 'A' && c <= 'Z') return static_cast<char>(c - 'A' + 'a');
  return c;
}

void setBit(unsigned char* bits, int c) { bits[c >> 3] |= static_cast<unsigned char>(1u << (c & 7)); }
}  // namespace

ProgramCompiler::ProgramCompiler() : _grammar(NULL), _program(NULL) {}

ProgramCompiler::~ProgramCompiler() {}

const std::string& ProgramCompiler::error() const { return _error; }

bool ProgramCompiler::fail(const std::string& message) {
  _error = "program: " + message;
  return false;
}

//< Returns the new instruction's address so a forward branch can be patched
//< once its target is known. That return value is the whole backpatch mechanism.
int ProgramCompiler::emit(Instruction::Op op, int x, int y) {
  Instruction ins;
  ins.op = op;
  ins.x = x;
  ins.y = y;
  _program->_code.push_back(ins);
  return static_cast<int>(_program->_code.size()) - 1;
}

int ProgramCompiler::addClass(const unsigned char* bits) {
  const std::size_t existing = _program->_classes.size() / 32;
  for (std::size_t i = 0; i < existing; ++i)
    if (std::memcmp(&_program->_classes[i * 32], bits, 32) == 0)
      return static_cast<int>(i);  //< dedupe · nospcrlfcl reused

  for (int i = 0; i < 32; ++i) _program->_classes.push_back(bits[i]);
  return static_cast<int>(existing);
}

//< "Can this whole subtree only ever match EXACTLY one octet?" If yes, emitNode
//< collapses it into a single Class op. Memoised, and the memo doubles as a
//< cycle guard: 2 ("no") is written on entry, so a recursive rule answers no
//< instead of looping forever.
bool ProgramCompiler::isSingleOctet(int node) const {
  const std::size_t index = static_cast<std::size_t>(node);
  if (_octetMemo.size() < index + 1) const_cast<std::vector<char>&>(_octetMemo).resize(index + 1, 0);
  if (_octetMemo[index] != 0) return _octetMemo[index] == 1;  //< memo · 2 set on entry also stops rule cycles

  const_cast<std::vector<char>&>(_octetMemo)[index] = 2;

  const GrammarNode& n = _grammar->node(node);
  bool yes = false;

  switch (n.kind) {
    case GrammarNode::OctetRange:
      yes = true;
      break;
    case GrammarNode::Literal:
      yes = (_grammar->literal(n.literal).size() == 1);
      break;
    case GrammarNode::Reference: {
      //< A CAPTURED reference answers no even when its body is one octet:
      //< folding it into a class would throw away the Save pair.
      const int root = _grammar->ruleRoot(n.lo);
      yes = (n.capture == GrammarNode::kNoCapture) && root != Grammar::kNoRule && isSingleOctet(root);
      break;
    }
    case GrammarNode::Alternation: {
      yes = (n.count > 0);
      for (int i = 0; i < n.count && yes; ++i)
        if (!isSingleOctet(_grammar->child(n.first + i))) yes = false;
      break;
    }
    default:
      yes = false;
      break;
  }

  const_cast<std::vector<char>&>(_octetMemo)[index] = yes ? 1 : 2;
  return yes;
}

//< Flattens a subtree's octets into one 256-bit table. `bits` is pre-zeroed by
//< the caller and OR'd into, so an Alternation just recurses into every branch.
bool ProgramCompiler::buildClass(int node, unsigned char* bits) const {
  const GrammarNode& n = _grammar->node(node);

  switch (n.kind) {
    case GrammarNode::OctetRange:
      for (int c = n.lo; c <= n.hi; ++c) setBit(bits, c);
      return true;
    case GrammarNode::Literal: {
      const std::string& text = _grammar->literal(n.literal);
      if (text.size() != 1) return false;  //< "x" folds into a class · "JOIN" cannot, it is 4 octets
      //< Set BOTH cases: RFC 5234 makes a quoted string case-insensitive.
      setBit(bits, static_cast<unsigned char>(text[0]));
      const char lower = fold(text[0]);
      const char upper = (lower >= 'a' && lower <= 'z') ? static_cast<char>(lower - 'a' + 'A') : lower;
      setBit(bits, static_cast<unsigned char>(lower));
      setBit(bits, static_cast<unsigned char>(upper));
      return true;
    }
    case GrammarNode::Reference: {
      const int root = _grammar->ruleRoot(n.lo);
      return root != Grammar::kNoRule && buildClass(root, bits);
    }
    case GrammarNode::Alternation:
      for (int i = 0; i < n.count; ++i)
        if (!buildClass(_grammar->child(n.first + i), bits)) return false;
      return true;
    default:
      return false;
  }
}

//< Does a $capture live anywhere under this node? Follows Reference nodes into
//< the rules they name, so it sees captures inlined from another rule too.
//< Used only by emitRepetition(), to decide whether a loop must be refused.
bool ProgramCompiler::hasCapture(int node) const {
  const GrammarNode& n = _grammar->node(node);

  if (n.kind == GrammarNode::Reference) {
    if (n.capture != GrammarNode::kNoCapture) return true;  //< a $capture lives under here
    const int root = _grammar->ruleRoot(n.lo);
    return root != Grammar::kNoRule && hasCapture(root);
  }
  for (int i = 0; i < n.count; ++i)
    if (hasCapture(_grammar->child(n.first + i))) return true;
  return false;
}

bool ProgramCompiler::emitBody(int node, int times) {
  for (int i = 0; i < times; ++i)
    if (!emitNode(node)) return false;
  return true;
}

//< Two shapes, chosen by whether there is an upper bound. See the file comment
//< for why the unbounded one cannot carry a capture.
bool ProgramCompiler::emitRepetition(int node) {
  const GrammarNode& n = _grammar->node(node);
  const int child = _grammar->child(n.first);
  const int least = n.lo < 0 ? 0 : n.lo;

  if (!emitBody(child, least)) return false;  //< the MANDATORY copies, emitted straight

  if (n.hi == GrammarNode::kUnbounded) {
    if (hasCapture(child))  //< REFUSED · *( $x ) reuses one slot pair, so only the last match would survive
      return fail(
          "a capture inside an unbounded repetition cannot be compiled: the "
          "loop reuses one slot pair, so only the last match would survive. "
          "Give the repetition an upper bound so it can be unrolled.");

    //< Unbounded: a real loop.  Split .x -> body, .y -> past the loop.
    //<   loop: Split body, exit
    //<   body: <child>
    //<         Jump loop
    //<   exit:
    const int loop = emit(Instruction::Split, 0, 0);
    _program->_code[static_cast<std::size_t>(loop)].x = static_cast<int>(_program->_code.size());
    if (!emitNode(child)) return false;
    emit(Instruction::Jump, loop, 0);
    _program->_code[static_cast<std::size_t>(loop)].y = static_cast<int>(_program->_code.size());
    return true;
  }

  const int optional = n.hi - least;
  if (optional < 0) return fail("repetition range runs backwards");  //< "5*2(x)"
  if (optional > kUnrollLimit)
    return fail("bounded repetition too large to unroll");  //< *14(x) unrolls · *999(x) will not

  //< Bounded: unroll the optional copies, each behind its own Split whose .y
  //< skips to the very end. Taking any exit therefore leaves the whole run --
  //< which is what makes the repetition greedy but escapable at every count.
  std::vector<int> exits;
  for (int i = 0; i < optional; ++i) {
    const int split = emit(Instruction::Split, 0, 0);
    _program->_code[static_cast<std::size_t>(split)].x = static_cast<int>(_program->_code.size());
    exits.push_back(split);
    if (!emitNode(child)) return false;
  }
  for (std::size_t i = 0; i < exits.size(); ++i)
    _program->_code[static_cast<std::size_t>(exits[i])].y = static_cast<int>(_program->_code.size());
  return true;
}

//< The recursive heart of the compiler: one case per node kind, after first
//< trying the single-octet class folding that keeps the programs small.
bool ProgramCompiler::emitNode(int node) {
  if (isSingleOctet(node)) {  //< collapse an alt-of-ranges into ONE Class op · nospcrlfcl -> 1 bitmap
    unsigned char bits[32];
    std::memset(bits, 0, sizeof(bits));
    if (buildClass(node, bits)) {
      emit(Instruction::Class, addClass(bits), 0);
      return true;
    }
  }

  const GrammarNode& n = _grammar->node(node);

  switch (n.kind) {
    case GrammarNode::OctetRange: {
      unsigned char bits[32];
      std::memset(bits, 0, sizeof(bits));
      for (int c = n.lo; c <= n.hi; ++c) setBit(bits, c);
      emit(Instruction::Class, addClass(bits), 0);
      return true;
    }

    case GrammarNode::Literal: {
      const std::string& text = _grammar->literal(n.literal);
      for (std::size_t i = 0; i < text.size(); ++i) {
        unsigned char bits[32];
        std::memset(bits, 0, sizeof(bits));
        const char lower = fold(text[i]);
        const char upper = (lower >= 'a' && lower <= 'z') ? static_cast<char>(lower - 'a' + 'A') : lower;
        setBit(bits, static_cast<unsigned char>(lower));
        setBit(bits, static_cast<unsigned char>(upper));
        emit(Instruction::Class, addClass(bits), 0);
      }
      return true;
    }

    case GrammarNode::Reference: {
      const int root = _grammar->ruleRoot(n.lo);
      if (root == Grammar::kNoRule) return fail("undefined rule reference");

      const std::size_t rule = static_cast<std::size_t>(n.lo);
      if (_compiling[rule])
        return fail("rule '" + _grammar->ruleName(n.lo) +
                    "' is recursive; the compiled strategy inlines rules and "
                    "cannot express recursion");

      if (n.capture == GrammarNode::kNoCapture) {
        _compiling[rule] = 1;
        const bool ok = emitNode(root);
        _compiling[rule] = 0;
        return ok;
      }

      const int slot = static_cast<int>(_program->_slotCapture.size());
      _program->_slotCapture.push_back(n.capture);

      emit(Instruction::Save, slot * 2, 0);
      _compiling[rule] = 1;
      const bool ok = emitNode(root);
      _compiling[rule] = 0;
      if (!ok) return false;
      emit(Instruction::Save, slot * 2 + 1, 0);
      return true;
    }

    case GrammarNode::Sequence:
      for (int i = 0; i < n.count; ++i)
        if (!emitNode(_grammar->child(n.first + i))) return false;
      return true;

    case GrammarNode::Alternation: {
      //< Per branch: Split (.x -> this branch, .y -> the next), body, Jump end.
      //< The LAST branch needs neither -- falling off it lands at the end
      //< anyway, and there is no next alternative to split toward.
      std::vector<int> jumps;
      for (int i = 0; i < n.count; ++i) {
        const bool last = (i == n.count - 1);
        int split = -1;
        if (!last) {
          split = emit(Instruction::Split, 0, 0);
          _program->_code[static_cast<std::size_t>(split)].x = static_cast<int>(_program->_code.size());
        }
        if (!emitNode(_grammar->child(n.first + i))) return false;
        if (!last) {
          jumps.push_back(emit(Instruction::Jump, 0, 0));
          _program->_code[static_cast<std::size_t>(split)].y = static_cast<int>(_program->_code.size());
        }
      }
      for (std::size_t i = 0; i < jumps.size(); ++i)
        _program->_code[static_cast<std::size_t>(jumps[i])].x = static_cast<int>(_program->_code.size());
      return true;
    }

    case GrammarNode::Repetition:
      return emitRepetition(node);
  }
  return fail("unknown node kind");
}

bool ProgramCompiler::compile(const Grammar& grammar, int rule, Program& out) {
  _error.clear();
  _grammar = &grammar;
  _program = &out;
  out.clear();

  _compiling.assign(grammar.ruleCount(), 0);
  _octetMemo.clear();

  const int root = grammar.ruleRoot(rule);
  if (root == Grammar::kNoRule) return fail("no such rule");

  if (!emitNode(root)) {
    out.clear();  //< cleared AGAIN so a caller never sees half a program
    return false;
  }
  emit(Instruction::Match, 0, 0);  //< always terminated, so a program cannot run off its end
  return true;
}

}  // namespace Compiled
}  // namespace Abnf
