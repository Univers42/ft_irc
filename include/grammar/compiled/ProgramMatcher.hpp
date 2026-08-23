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
class ProgramMatcher : public IMatcher {
 public:
  explicit ProgramMatcher(const Grammar& grammar);
  virtual ~ProgramMatcher();

  virtual bool match(int rule, const std::string& line, MatchResult& out) const;

  virtual const char* strategy() const;

  virtual bool lastExhausted() const;

  bool compileAll();

  const std::string& error() const;

 private:
  ProgramMatcher();
  ProgramMatcher(const ProgramMatcher& other);
  ProgramMatcher& operator=(const ProgramMatcher& other);

  struct Thread {
    int pc;
    int slots;
  };

  const Program* programFor(int rule) const;

  void addThread(std::vector<Thread>& list, int pc, int slots, std::size_t pos, std::vector<int>& seen, int generation,
                 const Program& program) const;

  int cloneSlots(int slots, int index, int value) const;

  const Grammar& _grammar;
  mutable std::vector<Program*> _programs;
  mutable std::string _error;

  mutable std::vector<std::vector<int> > _arena;
  mutable std::size_t _arenaUsed;
  mutable std::vector<int> _seen;
  mutable std::vector<Thread> _current;
  mutable std::vector<Thread> _next;
  mutable int _generation;
};

}  // namespace Compiled
}  // namespace Abnf

#endif
