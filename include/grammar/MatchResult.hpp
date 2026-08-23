#ifndef MATCHRESULT_HPP
#define MATCHRESULT_HPP

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

namespace Abnf {
class Grammar;

class MatchResult {
 public:
  MatchResult();
  MatchResult(const MatchResult& other);
  MatchResult& operator=(const MatchResult& other);
  ~MatchResult();

  bool has(const std::string& name) const;

  const std::string& get(const std::string& name) const;

  std::size_t count(const std::string& name) const;

  const std::string& at(const std::string& name, std::size_t index) const;

  const std::vector<std::string>& all(const std::string& name) const;

  /* A capture read as a separated list -- "#a,#b" as two fields rather than
  ** one string. Nothing about splitting a capture is protocol-specific, so it
  ** belongs beside at() rather than in whatever type happens to hold the
  ** result; Message had both of these and did nothing with them but forward.
  ** list() drops empty fields, which is what a comma-separated protocol list
  ** ("#a,,#b,") almost always means; listKeepEmpty() keeps them, for the
  ** parallel lists where position carries the meaning. */
  std::vector<std::string> list(const std::string& name, char separator) const;
  std::vector<std::string> listKeepEmpty(const std::string& name, char separator) const;

  void clear();

  void reset(const Grammar& grammar);

  void adopt(std::vector<std::vector<std::string> >& values);

  void adoptSequence(std::vector<std::string>& sequence, std::vector<int>& owners);

  std::size_t sequenceSize() const;
  const std::string& sequenceAt(std::size_t index) const;
  int sequenceOwner(std::size_t index) const;

 private:
  friend std::ostream& operator<<(std::ostream& os, const MatchResult& result);

  int slotOf(const std::string& name) const;

  const Grammar* _grammar;
  std::vector<std::vector<std::string> > _values;
  std::vector<std::string> _sequence;
  std::vector<int> _owners;
};

std::ostream& operator<<(std::ostream& os, const MatchResult& result);

}  // namespace Abnf

#endif
