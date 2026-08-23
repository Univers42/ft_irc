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

  std::size_t count(const std::string& name) const;

  const std::string& at(const std::string& name, std::size_t index) const;

  const std::vector<std::string>& all(const std::string& name) const;

  void clear();

  void reset(const Grammar& grammar);

  void adopt(std::vector<std::vector<std::string> >& values);

  void adoptSequence(std::vector<std::string>& sequence, std::vector<int>& owners);

  std::size_t sequenceSize() const;
  const std::string& sequenceAt(std::size_t index) const;
  int sequenceOwner(std::size_t index) const;

 private:
  int slotOf(const std::string& name) const;

  const Grammar* _grammar;
  std::vector<std::vector<std::string> > _values;
  std::vector<std::string> _sequence;
  std::vector<int> _owners;
};

}  // namespace Abnf

#endif
