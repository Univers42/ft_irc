#include "grammar/compiled/Program.hpp"

#include <ostream>
#include <vector>

/*
** Inert data plus accessors -- Program knows how to be READ; ProgramCompiler
** is the only thing that can write one, through friendship.
**
** The one piece of real logic here is inClass(). Character classes are 256-bit
** bitmaps stored as 32 bytes each, all of them packed end to end in one flat
** vector, so class N occupies bytes [N*32, N*32+32). Testing an octet is then
** two shifts and a mask:
**
**     byte  = classes[N*32 + (c >> 3)]     // which of the 32 bytes
**     bit   = 1 << (c & 7)                 // which bit inside it
**
** That is the VM's inner loop -- it runs once per live thread per input octet
** -- and it is why an alternation of five ranges like nospcrlfcl can collapse
** into ONE instruction instead of five that would each fork a thread.
*/
namespace Abnf {
namespace Compiled {
//< Defaults to Match, the one opcode that is harmless if never patched: a
//< stray default-constructed instruction ends its thread rather than jumping
//< somewhere arbitrary.
Instruction::Instruction() : op(Match), x(0), y(0) {}

Instruction::Instruction(const Instruction& other) : op(other.op), x(other.x), y(other.y) {}

Instruction& Instruction::operator=(const Instruction& other) {
  if (this != &other) {
    op = other.op;
    x = other.x;
    y = other.y;
  }
  return *this;
}

Instruction::~Instruction() {}

Program::Program() {}

Program::Program(const Program& other)
    : _code(other._code), _classes(other._classes), _slotCapture(other._slotCapture) {}

Program& Program::operator=(const Program& other) {
  if (this != &other) {
    _code = other._code;
    _classes = other._classes;
    _slotCapture = other._slotCapture;
  }
  return *this;
}

Program::~Program() {}

std::size_t Program::size() const { return _code.size(); }
const Instruction& Program::at(std::size_t pc) const { return _code[pc]; }
std::size_t Program::slotCount() const { return _slotCapture.size(); }
int Program::captureOfSlot(std::size_t slot) const { return _slotCapture[slot]; }

//< See the file comment: 32 bytes per class, flat. No bounds check -- the
//< index always comes from an Instruction::Class the compiler emitted.
bool Program::inClass(int classIndex, unsigned char c) const {
  const std::size_t base = static_cast<std::size_t>(classIndex) * 32;
  return (_classes[base + (c >> 3)] & (1u << (c & 7))) != 0;
}

void Program::clear() {
  _code.clear();
  _classes.clear();
  _slotCapture.clear();
}

bool Program::isEmpty() const { return _code.empty(); }

//< Disassembles one instruction, printing only the operands its opcode uses.
std::ostream& operator<<(std::ostream& os, const Instruction& ins) {
  //< Index-parallel to enum Op; a reorder there must be mirrored here.
  static const char* const kOpNames[] = {"Class", "Split", "Jump", "Save", "Match"};
  os << kOpNames[ins.op];
  switch (ins.op) {
    case Instruction::Class:
      os << " #" << ins.x;
      break;
    case Instruction::Split:
      os << " " << ins.x << ", " << ins.y;
      break;
    case Instruction::Jump:
      os << " " << ins.x;
      break;
    case Instruction::Save:
      os << " slot " << ins.x;
      break;
    default:
      break;
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, const Program& program) {
  os << "Program{" << program.size() << " instructions, " << program.slotCount() << " slots}";
  return os;
}

}  // namespace Compiled
}  // namespace Abnf
