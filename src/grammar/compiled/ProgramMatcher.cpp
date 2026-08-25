#include "grammar/compiled/ProgramMatcher.hpp"

#include <string>
#include <vector>

#include "grammar/compiled/ProgramCompiler.hpp"

/*
** The Pike VM -- a Thompson simulation with capture registers.
**
** Where TreeMatcher tries one alternative and backs up when it fails, this
** runs EVERY live alternative at once, advancing them all in lockstep, one
** input octet per step:
**
**     for each position in the line:
**         for each live thread:
**             Match -> accept, but ONLY if the line is finished too
**             Class -> if the octet is in the class, the thread survives into
**                      the next position; otherwise it dies right here
**
** Split, Jump and Save never survive a step: addThread() follows them the
** moment a thread arrives, so `current` and `next` only ever hold threads
** parked on a Class or a Match.
**
** ---- Why it cannot blow up ----
**
** The seen[]/generation pair in addThread() admits AT MOST ONE thread per
** program counter per input position. A backtracking matcher can revisit the
** same (pc, position) pair exponentially often; this one visits it once. That
** single check is the whole linear-time guarantee -- O(line x program) -- and
** it is why this strategy needs no step budget and lastExhausted() is
** hardwired to false.
**
** `generation` is just a cheap way to clear that table: rather than zeroing
** seen[] at every position, the current step number is stamped in, and any
** older stamp reads as "not seen this step".
**
** ---- Captures ----
**
** A thread carries an INDEX into _arena rather than its own register vector.
** Save clones the vector, writes one register, and hands the new index on, so
** threads that share a prefix share one register set until they diverge. The
** arena is reset per match() and its buffers are reused across calls.
*/
namespace Abnf {
namespace Compiled {
ProgramMatcher::ProgramMatcher(const Grammar& grammar) : _grammar(grammar), _arenaUsed(0), _generation(0) {
  _programs.assign(grammar.ruleCount(), static_cast<Program*>(NULL));
}
ProgramMatcher::~ProgramMatcher() {
  for (std::size_t i = 0; i < _programs.size(); ++i) delete _programs[i];
}
const char* ProgramMatcher::strategy() const { return "compiled/pike"; }
//< Never exhausted: there is no budget to run out of. @see the file comment.
bool ProgramMatcher::lastExhausted() const { return false; }
const std::string& ProgramMatcher::error() const { return _error; }

//< Lazy per-rule compile, memoised in _programs. compileAll() forces the whole
//< table up front so a grammar this strategy cannot express fails at STARTUP
//< rather than as a mysterious parse failure on some later message.
const Program* ProgramMatcher::programFor(int rule) const {
  if (rule < 0 || static_cast<std::size_t>(rule) >= _programs.size()) return NULL;

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

//< Copy-on-write for capture history. Threads share a register set until one
//< of them hits a Save; only then does it get its own copy. Sets are handed out
//< of _arena and never freed mid-match -- _arenaUsed is just a high-water mark,
//< reset per match(), so the vectors themselves are reused call after call.
int ProgramMatcher::cloneSlots(int slots, int index, int value) const {
  if (_arenaUsed == _arena.size()) _arena.push_back(std::vector<int>());
  const std::size_t fresh = _arenaUsed++;
  _arena[fresh] = _arena[static_cast<std::size_t>(slots)];
  _arena[fresh][static_cast<std::size_t>(index)] = value;
  return static_cast<int>(fresh);
}

//< Adds a thread at `pc`, resolving zero-width control flow immediately so the
//< thread list only ever holds Class and Match. The seen[] check at the top is
//< BOTH the linear-time guarantee and the termination guarantee: without it, a
//< loop of Split/Jump would recurse forever.
void ProgramMatcher::addThread(std::vector<Thread>& list, int pc, int slots, std::size_t pos, std::vector<int>& seen,
                               int generation, const Program& program) const {
  const std::size_t index = static_cast<std::size_t>(pc);
  if (seen[index] == generation) return;  //< one thread per pc per step · what keeps the VM LINEAR, not exponential
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
      //< Record where we are into register ins.x, then carry on at pc+1. The
      //< clone is what stops a later branch from overwriting a sibling's spans.
      const int updated = cloneSlots(slots, ins.x, static_cast<int>(pos));
      addThread(list, pc + 1, updated, pos, seen, generation, program);
      return;
    }

    case Instruction::Class:
    case Instruction::Match: {
      //< These two are the only ops that end a step; everything else was
      //< followed above. Park the thread and let match()'s loop drive it.
      Thread thread;
      thread.pc = pc;
      thread.slots = slots;
      list.push_back(thread);
      return;
    }
  }
}

bool ProgramMatcher::match(int rule, const std::string& line, MatchResult& out) const {
  out.reset(_grammar);

  const Program* program = programFor(rule);
  if (program == NULL || program->isEmpty()) return false;

  const std::size_t slots = program->slotCount() * 2;  //< two registers per slot: start, end

  //< Arena entry 0 is the initial register set: -1 everywhere, meaning "this
  //< capture never fired". Reusing the vectors across calls is what keeps the
  //< steady state allocation-free.
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

  addThread(current, 0, 0, 0, seen, ++generation, *program);  //< one thread, at pc 0, position 0

  const std::size_t length = line.size();
  for (std::size_t pos = 0;; ++pos) {
    next.clear();
    ++generation;

    for (std::size_t i = 0; i < current.size(); ++i) {
      const Thread& thread = current[i];
      const Instruction& ins = program->at(static_cast<std::size_t>(thread.pc));

      if (ins.op == Instruction::Match) {  //< a thread reached the end · accept only if the line did too
        if (pos != length) continue;       //< matched a PREFIX · "JOIN #a junk" is not a JOIN

        std::vector<std::vector<std::string> > values(_grammar.captureCount(), std::vector<std::string>());
        std::vector<std::string> sequence;
        std::vector<int> owners;
        //< Turn the winning thread's registers into text. Both views are built
        //< here in one pass: `values` by slot, `sequence`/`owners` in order.
        for (std::size_t slot = 0; slot < program->slotCount(); ++slot) {
          const std::vector<int>& saved = _arena[static_cast<std::size_t>(thread.slots)];
          const int start = saved[slot * 2];
          const int end = saved[slot * 2 + 1];
          if (start < 0 || end < 0 || end < start) continue;  //< slot never fired
          const int capture = program->captureOfSlot(slot);
          const std::string text = line.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start));
          values[static_cast<std::size_t>(capture)].push_back(text);
          sequence.push_back(text);
          owners.push_back(capture);
        }
        out.adopt(values);
        out.adoptSequence(sequence, owners);
        _generation = generation;
        return true;
      }

      if (ins.op == Instruction::Class && pos < length) {  //< consume one octet if it is in the class bitmap
        const unsigned char c = static_cast<unsigned char>(line[pos]);
        if (program->inClass(ins.x, c))
          addThread(next, thread.pc + 1, thread.slots, pos + 1, seen, generation, *program);
      }
    }

    //< Loop runs to length INCLUSIVE: the final pass has no octet to consume
    //< and exists only to let a Match thread accept at the end of the line.
    if (pos >= length) break;  //< end of input · only Match threads still matter
    current.swap(next);
    if (current.empty()) break;  //< every thread died · no path can match, stop early
  }

  _generation = generation;
  return false;
}

}  // namespace Compiled
}  // namespace Abnf
