#include "grammar/compiled/Program.hpp"

#include <ostream>
#include <vector>

namespace Abnf {
namespace Compiled {
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

std::ostream& operator<<(std::ostream& os, const Instruction& ins) {
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
