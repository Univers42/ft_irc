#ifndef PROGRAMCOMPILER_HPP
#define PROGRAMCOMPILER_HPP

#include <string>
#include <vector>

#include "grammar/Grammar.hpp"
#include "grammar/compiled/Program.hpp"

namespace Abnf {
namespace Compiled {
class ProgramCompiler {
 public:
  ProgramCompiler();

  bool compile(const Grammar& grammar, int rule, Program& out);

  const std::string& error() const;

 private:
  ProgramCompiler(const ProgramCompiler& other);
  ProgramCompiler& operator=(const ProgramCompiler& other);

  int emit(Instruction::Op op, int x, int y);
  bool emitNode(int node);
  bool emitRepetition(int node);
  bool emitBody(int node, int times);

  int addClass(const unsigned char* bits);
  bool buildClass(int node, unsigned char* bits) const;
  bool isSingleOctet(int node) const;

  bool fail(const std::string& message);

  const Grammar* _grammar;
  Program* _program;
  std::vector<char> _compiling;
  std::vector<char> _octetMemo;
  std::string _error;
};

}  // namespace Compiled
}  // namespace Abnf

#endif
