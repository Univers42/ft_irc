/**
 * @file Program.hpp
 * @brief Bytecode for the compiled matching strategy.
 *
 * The COMPILED half of the module. One Grammar rule compiles to one Program:
 * @verbatim
 *   Grammar rule --ProgramCompiler--> Program --ProgramMatcher--> MatchResult
 *                                     ^^^^^^^
 * @endverbatim
 *
 * @section pr_why Why a bytecode at all
 *
 * TreeMatcher walks the AST and backtracks, which needs a step budget to stay
 * bounded. This side takes the other classic route (Thompson 1968, Pike's VM):
 * flatten the rule into a straight-line instruction list, then run every
 * alternative *at the same time*, one input octet per step. No backtracking,
 * so no budget -- the cost is linear in line length times program size.
 *
 * @section pr_isa The instruction set
 *
 * Five ops, and they are all you need for a regular language:
 * @verbatim
 *   Class #n     consume one octet if it is in bitmap n, else this thread dies
 *   Split x, y   fork: continue at BOTH x and y, in the same input position
 *   Jump x       continue at x
 *   Save n       record the current input position into slot n
 *   Match        this thread has reached the end of the rule
 * @endverbatim
 *
 * Alternation becomes Split, repetition becomes Split plus Jump, and a capture
 * becomes a Save pair bracketing its body. Sequence needs no instruction at
 * all -- it is just adjacency.
 *
 * @section pr_cls Character classes
 *
 * A class is a 256-bit bitmap, stored as 32 bytes, all of them packed end to
 * end in one flat vector. That collapses an alternation of octet ranges --
 * @c nospcrlfcl is five of them -- into a single Class op with one table
 * lookup, instead of five instructions that would each fork a thread.
 *
 * @note Program is inert data. It knows how to be read; ProgramCompiler is
 *       the only thing that can write one, via friendship.
 */
#ifndef PROGRAM_HPP
#define PROGRAM_HPP

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

namespace Abnf {
namespace Compiled {
/** @brief One VM instruction: an opcode and two int operands. */
struct Instruction {
  /** @brief The five opcodes. @see the file comment for what each does. */
  enum Op {
    Class,  //< x = class index. Consumes one octet if the bitmap has its bit.
    Split,  //< x, y = two program counters. Forks; both are tried.
    Jump,   //< x = program counter. Unconditional.
    Save,   //< x = slot number. Stores the current input offset.
    Match   //< Accepting state. Operands unused.
  };

  Op op;  //< Which instruction this is.
  int x;  //< First operand; meaning depends on `op`, see the enum.
  int y;  //< Second operand; used by Split only.

  /** @brief Defaults to Match with zero operands -- a harmless placeholder. */
  Instruction();
  Instruction(const Instruction& other);
  Instruction& operator=(const Instruction& other);
  ~Instruction();
};

/** @brief Debug dump of one instruction, e.g. "Split 12, 40". */
std::ostream& operator<<(std::ostream& os, const Instruction& ins);

/** @brief A compiled rule: code, class bitmaps and the capture slot map. */
class Program {
 public:
  Program();
  Program(const Program& other);
  Program& operator=(const Program& other);
  ~Program();

  /** @brief @return Number of instructions; also the one-past-end address. */
  std::size_t size() const;

  /** @brief @return The instruction at @p pc. @warning Unchecked. */
  const Instruction& at(std::size_t pc) const;

  /**
   * @brief Tests one octet against one character class.
   * @param classIndex Class number, as carried in an Instruction::Class's `x`.
   * @param c          The octet to test.
   * @return true if @p c is in the class.
   * @note Two shifts and a mask over the flat 32-bytes-per-class table. This
   *       is the VM's inner loop, executed once per thread per input octet.
   */
  bool inClass(int classIndex, unsigned char c) const;

  /**
   * @brief @return How many capture slots this program uses.
   * @note Each slot needs TWO save registers, at 2*slot and 2*slot+1, holding
   *       the start and end offsets. ProgramMatcher sizes its arena from this.
   */
  std::size_t slotCount() const;

  /**
   * @brief Maps a slot back to the grammar capture it came from.
   * @param slot Slot number, in [0, slotCount()).
   * @return Index into Grammar::_captureNames.
   * @note The indirection exists because one capture name can be compiled
   *       several times -- once per place it appears -- and each occurrence
   *       needs its own pair of registers while still reporting the same name.
   */
  int captureOfSlot(std::size_t slot) const;

  /** @brief Drops code, classes and slots; used before a recompile. */
  void clear();

  /** @brief @return true if nothing has been emitted yet. */
  bool isEmpty() const;

 private:
  //< ProgramCompiler emits straight into these, which keeps every accessor
  //< above const and read-only for the VM.
  friend class ProgramCompiler;

  std::vector<Instruction> _code;       //< The program; index == program counter.
  std::vector<unsigned char> _classes;  //< Flat 32-byte bitmaps, back to back.
  std::vector<int> _slotCapture;        //< _slotCapture[slot] = capture index.
};

/** @brief Debug dump: instruction and slot counts, not a disassembly. */
std::ostream& operator<<(std::ostream& os, const Program& program);

}  // namespace Compiled
}  // namespace Abnf

#endif
