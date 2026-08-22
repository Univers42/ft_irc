#ifndef MATCHRESULT_HPP
#define MATCHRESULT_HPP

#include <cstddef>
#include <string>
#include <vector>

namespace Abnf {
class Grammar;

class MatchResult {
 public:
  MatchResult();

  bool has(const std::string& name) const;

  const std::string& get(const std::string& name) const;

  void clear();

  void reset(const Grammar& grammar);

  void adopt(std::vector<std::string>& values, std::vector<char>& present);

 private:
  int slotOf(const std::string& name) const;

  const Grammar* _grammar;
  std::vector<std::string> _values;
  std::vector<char> _present;
};

}  // namespace Abnf

#endif
