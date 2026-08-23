#ifndef PROGRAM_HPP
#define PROGRAM_HPP

#include <cstddef>
#include <string>
#include <vector>

namespace Abnf {
namespace Compiled {
struct Instruction {
  enum Op { Class, Split, Jump, Save, Match };

  Op op;
  int x;
  int y;

  Instruction();
};

class Program {
 public:
  Program();

  std::size_t size() const;
  const Instruction& at(std::size_t pc) const;

  bool inClass(int classIndex, unsigned char c) const;

  std::size_t slotCount() const;
  int captureOfSlot(std::size_t slot) const;

  void clear();
  bool isEmpty() const;

 private:
  friend class ProgramCompiler;

  std::vector<Instruction> _code;
  std::vector<unsigned char> _classes;
  std::vector<int> _slotCapture;
};

}  // namespace Compiled
}  // namespace Abnf

#endif
