#ifndef MATCHRESULT_HPP
#define MATCHRESULT_HPP

#include <cstddef>
#include <string>
#include <vector>

class Grammar;

/* What a successful match recorded, keyed by the `$name` markers that produced
** it.
**
** has() and get() are separate on purpose. "absent" and "present but empty"
** are different answers -- `TOPIC #c` queries the topic while `TOPIC #c :`
** clears it, and telling those apart is the whole reason captures exist.
*/
class MatchResult {
 public:
  MatchResult();

  bool has(const std::string& name) const;

  /* The captured span, or an empty string when the name was not captured or is
  ** not in the grammar at all. Ask has() first when the difference matters. */
  const std::string& get(const std::string& name) const;

  void clear();

 private:
  friend class GrammarMatcher;

  int slotOf(const std::string& name) const;

  const Grammar* _grammar;
  std::vector<std::string> _values;
  std::vector<char> _present;
};

#endif /* MATCHRESULT_HPP */
