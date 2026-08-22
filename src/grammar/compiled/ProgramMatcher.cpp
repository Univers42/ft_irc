#include "grammar/compiled/ProgramMatcher.hpp"

#include <string>
#include <vector>

#include "grammar/compiled/ProgramCompiler.hpp"

namespace Abnf {
namespace Compiled {
ProgramMatcher::ProgramMatcher(const Grammar& grammar)
    : _grammar(grammar), _arenaUsed(0), _generation(0) {
  _programs.assign(grammar.ruleCount(), static_cast<Program*>(NULL));
}

ProgramMatcher::~ProgramMatcher() {
  for (std::size_t i = 0; i < _programs.size(); ++i) delete _programs[i];
}

const char* ProgramMatcher::strategy() const { return "compiled/pike"; }

bool ProgramMatcher::lastExhausted() const { return false; }

const std::string& ProgramMatcher::error() const { return _error; }

const Program* ProgramMatcher::programFor(int rule) const {
  if (rule < 0 || static_cast<std::size_t>(rule) >= _programs.size())
    return NULL;

  const std::size_t index = static_cast<std::size_t>(rule);
  if (_programs[index] != NULL) return _programs[index];

  Program* program = new Program();
  ProgramCompiler compiler;
  if (!compiler.compile(_grammar, rule, *program)) {
    _error = compiler.error();
    delete program;
    return NULL;
  }
  _programs[index] = program;
  return program;
}

bool ProgramMatcher::compileAll() {
  for (std::size_t i = 0; i < _programs.size(); ++i) {
    if (_grammar.ruleRoot(static_cast<int>(i)) == Grammar::kNoRule) continue;
    if (programFor(static_cast<int>(i)) == NULL) return false;
  }
  return true;
}

int ProgramMatcher::cloneSlots(int slots, int index, int value) const {
  if (_arenaUsed == _arena.size()) _arena.push_back(std::vector<int>());
  const std::size_t fresh = _arenaUsed++;
  _arena[fresh] = _arena[static_cast<std::size_t>(slots)];
  _arena[fresh][static_cast<std::size_t>(index)] = value;
  return static_cast<int>(fresh);
}

void ProgramMatcher::addThread(std::vector<Thread>& list, int pc, int slots,
                               std::size_t pos, std::vector<int>& seen,
                               int generation, const Program& program) const {
  const std::size_t index = static_cast<std::size_t>(pc);
  if (seen[index] == generation) return;
  seen[index] = generation;

  const Instruction& ins = program.at(index);

  switch (ins.op) {
    case Instruction::Jump:
      addThread(list, ins.x, slots, pos, seen, generation, program);
      return;

    case Instruction::Split:
      addThread(list, ins.x, slots, pos, seen, generation, program);
      addThread(list, ins.y, slots, pos, seen, generation, program);
      return;

    case Instruction::Save: {
      const int updated = cloneSlots(slots, ins.x, static_cast<int>(pos));
      addThread(list, pc + 1, updated, pos, seen, generation, program);
      return;
    }

    case Instruction::Class:
    case Instruction::Match: {
      Thread thread;
      thread.pc = pc;
      thread.slots = slots;
      list.push_back(thread);
      return;
    }
  }
}

bool ProgramMatcher::match(int rule, const std::string& line,
                           MatchResult& out) const {
  out.reset(_grammar);

  const Program* program = programFor(rule);
  if (program == NULL || program->isEmpty()) return false;

  const std::size_t slots = program->slotCount() * 2;

  if (_arena.empty()) _arena.push_back(std::vector<int>());
  _arena[0].assign(slots, -1);
  _arenaUsed = 1;

  _seen.assign(program->size(), 0);
  std::vector<int>& seen = _seen;
  int generation = _generation;

  std::vector<Thread>& current = _current;
  std::vector<Thread>& next = _next;
  current.clear();
  next.clear();

  addThread(current, 0, 0, 0, seen, ++generation, *program);

  const std::size_t length = line.size();
  for (std::size_t pos = 0;; ++pos) {
    next.clear();
    ++generation;

    for (std::size_t i = 0; i < current.size(); ++i) {
      const Thread& thread = current[i];
      const Instruction& ins = program->at(static_cast<std::size_t>(thread.pc));

      if (ins.op == Instruction::Match) {
        if (pos != length) continue;

        std::vector<std::vector<std::string> > values(
            _grammar.captureCount(), std::vector<std::string>());
        std::vector<std::string> sequence;
        std::vector<int> owners;
        for (std::size_t slot = 0; slot < program->slotCount(); ++slot) {
          const std::vector<int>& saved =
              _arena[static_cast<std::size_t>(thread.slots)];
          const int start = saved[slot * 2];
          const int end = saved[slot * 2 + 1];
          if (start < 0 || end < 0 || end < start) continue;
          const int capture = program->captureOfSlot(slot);
          const std::string text =
              line.substr(static_cast<std::size_t>(start),
                          static_cast<std::size_t>(end - start));
          values[static_cast<std::size_t>(capture)].push_back(text);
          sequence.push_back(text);
          owners.push_back(capture);
        }
        out.adopt(values);
        out.adoptSequence(sequence, owners);
        _generation = generation;
        return true;
      }

      if (ins.op == Instruction::Class && pos < length) {
        const unsigned char c = static_cast<unsigned char>(line[pos]);
        if (program->inClass(ins.x, c))
          addThread(next, thread.pc + 1, thread.slots, pos + 1, seen,
                    generation, *program);
      }
    }

    if (pos >= length) break;
    current.swap(next);
    if (current.empty()) break;
  }

  _generation = generation;
  return false;
}

}  // namespace Compiled
}  // namespace Abnf
