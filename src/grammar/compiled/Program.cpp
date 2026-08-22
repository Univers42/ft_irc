#include "grammar/compiled/Program.hpp"

#include <vector>

namespace Abnf {
namespace Compiled {
Instruction::Instruction() : op(Match), x(0), y(0) {}

Program::Program() {}

std::size_t Program::size() const { return _code.size(); }

const Instruction& Program::at(std::size_t pc) const { return _code[pc]; }

bool Program::inClass(int classIndex, unsigned char c) const {
  const std::size_t base = static_cast<std::size_t>(classIndex) * 32;
  return (_classes[base + (c >> 3)] & (1u << (c & 7))) != 0;
}

std::size_t Program::slotCount() const { return _slotCapture.size(); }

int Program::captureOfSlot(std::size_t slot) const {
  return _slotCapture[slot];
}

void Program::clear() {
  _code.clear();
  _classes.clear();
  _slotCapture.clear();
}

bool Program::isEmpty() const { return _code.empty(); }

}  // namespace Compiled
}  // namespace Abnf
