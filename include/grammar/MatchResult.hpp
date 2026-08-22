#ifndef MATCHRESULT_HPP
#define MATCHRESULT_HPP

#include <cstddef>
#include <string>
#include <vector>

class Grammar;

class MatchResult {
 public:
  MatchResult();

  bool has(const std::string& name) const;

  const std::string& get(const std::string& name) const;

  void clear();

 private:
  friend class GrammarMatcher;

  int slotOf(const std::string& name) const;

  const Grammar* _grammar;
  std::vector<std::string> _values;
  std::vector<char> _present;
};

#endif
