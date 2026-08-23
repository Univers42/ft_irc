#include "grammar/MatchResult.hpp"

#include <string>
#include <vector>

#include "grammar/Grammar.hpp"

namespace Abnf {
namespace {
const std::string& emptyString() {
  static const std::string kEmpty;
  return kEmpty;
}

const std::vector<std::string>& emptyList() {
  static const std::vector<std::string> kEmpty;
  return kEmpty;
}

}  // namespace

MatchResult::MatchResult() : _grammar(NULL) {}

int MatchResult::slotOf(const std::string& name) const {
  if (_grammar == NULL) return -1;
  for (std::size_t i = 0; i < _grammar->captureCount(); ++i)
    if (_grammar->captureName(static_cast<int>(i)) == name) return static_cast<int>(i);
  return -1;
}

bool MatchResult::has(const std::string& name) const {
  const int slot = slotOf(name);
  if (slot < 0) return false;
  return !_values[static_cast<std::size_t>(slot)].empty();
}

std::size_t MatchResult::count(const std::string& name) const {
  const int slot = slotOf(name);
  if (slot < 0) return 0;
  return _values[static_cast<std::size_t>(slot)].size();
}

const std::string& MatchResult::get(const std::string& name) const { return at(name, 0); }

const std::string& MatchResult::at(const std::string& name, std::size_t index) const {
  const int slot = slotOf(name);
  if (slot < 0) return emptyString();
  const std::vector<std::string>& list = _values[static_cast<std::size_t>(slot)];
  if (index >= list.size()) return emptyString();
  return list[index];
}

const std::vector<std::string>& MatchResult::all(const std::string& name) const {
  const int slot = slotOf(name);
  if (slot < 0) return emptyList();
  return _values[static_cast<std::size_t>(slot)];
}

std::size_t MatchResult::sequenceSize() const { return _sequence.size(); }

const std::string& MatchResult::sequenceAt(std::size_t index) const {
  if (index >= _sequence.size()) return emptyString();
  return _sequence[index];
}

int MatchResult::sequenceOwner(std::size_t index) const {
  if (index >= _owners.size()) return -1;
  return _owners[index];
}

void MatchResult::adoptSequence(std::vector<std::string>& sequence, std::vector<int>& owners) {
  _sequence.swap(sequence);
  _owners.swap(owners);
}

void MatchResult::clear() {
  _sequence.clear();
  _owners.clear();
  _values.clear();
  _grammar = NULL;
}

void MatchResult::reset(const Grammar& grammar) {
  _grammar = &grammar;
  _sequence.clear();
  _owners.clear();
  _values.assign(grammar.captureCount(), std::vector<std::string>());
}

void MatchResult::adopt(std::vector<std::vector<std::string> >& values) { _values.swap(values); }

}  // namespace Abnf
