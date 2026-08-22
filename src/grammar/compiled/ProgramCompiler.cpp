#include "grammar/compiled/ProgramCompiler.hpp"

#include <cstring>
#include <string>
#include <vector>

#include "grammar/GrammarNode.hpp"

namespace Abnf {
namespace Compiled {
namespace {
const int kUnrollLimit = 64;

char fold(char c) {
  if (c >= 'A' && c <= 'Z') return static_cast<char>(c - 'A' + 'a');
  return c;
}

void setBit(unsigned char* bits, int c) {
  bits[c >> 3] |= static_cast<unsigned char>(1u << (c & 7));
}

}  // namespace

ProgramCompiler::ProgramCompiler() : _grammar(NULL), _program(NULL) {}

const std::string& ProgramCompiler::error() const { return _error; }

bool ProgramCompiler::fail(const std::string& message) {
  _error = "program: " + message;
  return false;
}

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
      return static_cast<int>(i);

  for (int i = 0; i < 32; ++i) _program->_classes.push_back(bits[i]);
  return static_cast<int>(existing);
}

bool ProgramCompiler::isSingleOctet(int node) const {
  const std::size_t index = static_cast<std::size_t>(node);
  if (_octetMemo.size() < index + 1)
    const_cast<std::vector<char>&>(_octetMemo).resize(index + 1, 0);
  if (_octetMemo[index] != 0) return _octetMemo[index] == 1;

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
      const int root = _grammar->ruleRoot(n.lo);
      yes = (n.capture == GrammarNode::kNoCapture) &&
            root != Grammar::kNoRule && isSingleOctet(root);
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

bool ProgramCompiler::buildClass(int node, unsigned char* bits) const {
  const GrammarNode& n = _grammar->node(node);

  switch (n.kind) {
    case GrammarNode::OctetRange:
      for (int c = n.lo; c <= n.hi; ++c) setBit(bits, c);
      return true;
    case GrammarNode::Literal: {
      const std::string& text = _grammar->literal(n.literal);
      if (text.size() != 1) return false;
      setBit(bits, static_cast<unsigned char>(text[0]));
      const char lower = fold(text[0]);
      const char upper = (lower >= 'a' && lower <= 'z')
                             ? static_cast<char>(lower - 'a' + 'A')
                             : lower;
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

bool ProgramCompiler::emitBody(int node, int times) {
  for (int i = 0; i < times; ++i)
    if (!emitNode(node)) return false;
  return true;
}

bool ProgramCompiler::emitRepetition(int node) {
  const GrammarNode& n = _grammar->node(node);
  const int child = _grammar->child(n.first);
  const int least = n.lo < 0 ? 0 : n.lo;

  if (!emitBody(child, least)) return false;

  if (n.hi == GrammarNode::kUnbounded) {
    const int loop = emit(Instruction::Split, 0, 0);
    _program->_code[static_cast<std::size_t>(loop)].x =
        static_cast<int>(_program->_code.size());
    if (!emitNode(child)) return false;
    emit(Instruction::Jump, loop, 0);
    _program->_code[static_cast<std::size_t>(loop)].y =
        static_cast<int>(_program->_code.size());
    return true;
  }

  const int optional = n.hi - least;
  if (optional < 0) return fail("repetition range runs backwards");
  if (optional > kUnrollLimit)
    return fail("bounded repetition too large to unroll");

  std::vector<int> exits;
  for (int i = 0; i < optional; ++i) {
    const int split = emit(Instruction::Split, 0, 0);
    _program->_code[static_cast<std::size_t>(split)].x =
        static_cast<int>(_program->_code.size());
    exits.push_back(split);
    if (!emitNode(child)) return false;
  }
  for (std::size_t i = 0; i < exits.size(); ++i)
    _program->_code[static_cast<std::size_t>(exits[i])].y =
        static_cast<int>(_program->_code.size());
  return true;
}

bool ProgramCompiler::emitNode(int node) {
  if (isSingleOctet(node)) {
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
        const char upper = (lower >= 'a' && lower <= 'z')
                               ? static_cast<char>(lower - 'a' + 'A')
                               : lower;
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
      std::vector<int> jumps;
      for (int i = 0; i < n.count; ++i) {
        const bool last = (i == n.count - 1);
        int split = -1;
        if (!last) {
          split = emit(Instruction::Split, 0, 0);
          _program->_code[static_cast<std::size_t>(split)].x =
              static_cast<int>(_program->_code.size());
        }
        if (!emitNode(_grammar->child(n.first + i))) return false;
        if (!last) {
          jumps.push_back(emit(Instruction::Jump, 0, 0));
          _program->_code[static_cast<std::size_t>(split)].y =
              static_cast<int>(_program->_code.size());
        }
      }
      for (std::size_t i = 0; i < jumps.size(); ++i)
        _program->_code[static_cast<std::size_t>(jumps[i])].x =
            static_cast<int>(_program->_code.size());
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
    out.clear();
    return false;
  }
  emit(Instruction::Match, 0, 0);
  return true;
}

}  // namespace Compiled
}  // namespace Abnf
