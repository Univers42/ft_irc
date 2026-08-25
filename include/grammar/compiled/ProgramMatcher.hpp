/**
 * @file ProgramMatcher.hpp
 * @brief The Pike VM: runs compiled Programs against a line.
 *
 * The execution engine of the compiled strategy, and one of the two IMatcher
 * implementations:
 * @verbatim
 *   Grammar rule --ProgramCompiler--> Program --ProgramMatcher--> MatchResult
 *                                               ^^^^^^^^^^^^^^
 * @endverbatim
 *
 * @section pm_algo The algorithm
 *
 * Thompson simulation with capture registers, i.e. a Pike VM. Instead of
 * trying one alternative and backing up when it fails, it advances EVERY live
 * alternative in lockstep, one input octet per step:
 *
 * @verbatim
 *   for each position in the line:
 *       for each live thread:
 *           Match  -> accept, but only if the line is also finished
 *           Class  -> if the octet is in the class, the thread survives
 *                     into the next position; otherwise it dies here
 *   (Split, Jump and Save never survive a step -- addThread follows them
 *    immediately, so the thread list only ever holds Class and Match.)
 * @endverbatim
 *
 * @section pm_lin Why it cannot blow up
 *
 * The `seen`/`generation` pair in addThread() admits at most one thread per
 * program counter per input position. A backtracking matcher can visit the
 * same (pc, position) pair exponentially many times; this one visits it once.
 * That single check is what makes the whole thing linear -- O(line x program)
 * -- and it is why this matcher needs no step budget and lastExhausted() is
 * hardwired to false.
 *
 * The generation counter is a cheap way to reset that table: instead of
 * clearing `seen` at every position, the current step number is stamped into
 * it, and a stale stamp reads as "not seen".
 *
 * @section pm_cap Captures
 *
 * Each thread carries an index into an arena of register vectors rather than
 * its own copy. A Save clones the vector, writes one register and hands the
 * new index to the successor thread, so threads share history up to the point
 * they diverged. The arena is reset per match() and reused across matches.
 *
 * @section pm_mut On all the mutable members
 *
 * IMatcher::match() is @c const, but this class caches compiled programs
 * lazily and reuses its scratch buffers between calls. Everything @c mutable
 * below is one of those two things: a cache or a buffer. None of it is
 * observable through the public interface, so the constness is honest.
 *
 * @warning Consequently NOT thread-safe. One matcher per thread, if it ever
 *          comes to that. The server is single-threaded, so it does not.
 */
#ifndef PROGRAMMATCHER_HPP
#define PROGRAMMATCHER_HPP

#include <string>
#include <vector>

#include "grammar/Grammar.hpp"
#include "grammar/IMatcher.hpp"
#include "grammar/MatchResult.hpp"
#include "grammar/compiled/Program.hpp"

namespace Abnf {
namespace Compiled {
/** @brief IMatcher backed by compiled bytecode; @see the file comment. */
class ProgramMatcher : public IMatcher {
 public:
  /**
   * @brief Binds to @p grammar; compiles nothing yet.
   * @param grammar Borrowed by reference, so it must outlive the matcher.
   * @note Programs are built on first use per rule. Call compileAll() to force
   *       them all up front and surface any compile error at startup instead.
   */
  explicit ProgramMatcher(const Grammar& grammar);

  /** @brief Deletes every program compiled so far. */
  virtual ~ProgramMatcher();

  /**
   * @brief Runs rule @p rule's program over @p line. @copydoc IMatcher::match
   * @note Returns false both for "does not match" and for "could not compile
   *       this rule"; error() tells the two apart.
   */
  virtual bool match(int rule, const std::string& line, MatchResult& out) const;

  /** @brief @return The literal "compiled/pike". */
  virtual const char* strategy() const;

  /**
   * @brief @return Always false -- this strategy has no budget to exhaust.
   * @note @see the pm_lin section for why that is guaranteed, not hopeful.
   */
  virtual bool lastExhausted() const;

  /**
   * @brief Compiles every defined rule up front.
   * @return true if all of them compiled; false with error() naming the first
   *         rule that did not.
   * @note Server::initGrammar() calls this and refuses to start on failure, so
   *       a grammar the compiled strategy cannot express is a startup error
   *       rather than a mysterious per-message parse failure.
   */
  bool compileAll();

  /** @brief @return The last compilation failure, or "". */
  const std::string& error() const;

 private:
  //< No default ctor (there is no grammar to bind to) and non-copyable
  //< (_programs holds owning raw pointers, so a copy would double-free).
  ProgramMatcher();
  ProgramMatcher(const ProgramMatcher& other);
  ProgramMatcher& operator=(const ProgramMatcher& other);

  /**
   * @brief One live alternative inside the VM.
   * @note Deliberately two ints: threads are copied constantly, so the capture
   *       registers live in the arena and only their index travels here.
   */
  struct Thread {
    int pc;     //< Program counter; always at a Class or Match instruction.
    int slots;  //< Index into _arena of this thread's capture registers.
  };

  /**
   * @brief Returns rule @p rule's program, compiling it on first use.
   * @return The program, or NULL if @p rule is out of range or would not
   *         compile -- in which case error() is set.
   */
  const Program* programFor(int rule) const;

  /**
   * @brief Adds a thread at @p pc, resolving control flow immediately.
   * @param[in,out] list  Thread list to append to (current or next step).
   * @param pc            Where the new thread starts.
   * @param slots         Its capture-register index in _arena.
   * @param pos           Current input offset; what a Save would record.
   * @param[in,out] seen  Per-pc generation stamps, the dedup table.
   * @param generation    This step's stamp.
   * @param program       The program being run.
   * @note Follows Jump, Split and Save recursively so the list only ever holds
   *       Class and Match threads. The @p seen check at the top is the linear-
   *       time guarantee AND the termination guarantee: without it, a loop of
   *       zero-width instructions would recurse forever.
   */
  void addThread(std::vector<Thread>& list, int pc, int slots, std::size_t pos, std::vector<int>& seen, int generation,
                 const Program& program) const;

  /**
   * @brief Copies register set @p slots, sets register @p index to @p value.
   * @return Index of the fresh register set in _arena.
   * @note Copy-on-write for capture history: threads sharing a prefix share
   *       one register set until one of them hits a Save.
   */
  int cloneSlots(int slots, int index, int value) const;

  const Grammar& _grammar;                  //< Borrowed; outlives this matcher.
  mutable std::vector<Program*> _programs;  //< OWNED, one per rule, NULL until compiled.
  mutable std::string _error;               //< Last compile failure.

  //< Scratch, reused across match() calls to keep the steady state allocation-free.
  mutable std::vector<std::vector<int> > _arena;  //< Capture register sets.
  mutable std::size_t _arenaUsed;                 //< High-water mark within _arena.
  mutable std::vector<int> _seen;                 //< Per-pc generation stamps.
  mutable std::vector<Thread> _current;           //< Threads at this position.
  mutable std::vector<Thread> _next;              //< Threads surviving into the next.
  mutable int _generation;                        //< Monotonic stamp; see pm_lin.
};

}  // namespace Compiled
}  // namespace Abnf

#endif
