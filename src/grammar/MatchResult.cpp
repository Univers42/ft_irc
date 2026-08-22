#include "grammar/MatchResult.hpp"

#include "grammar/Grammar.hpp"

namespace {
const std::string kEmpty;
}  // namespace

MatchResult::MatchResult() : _grammar(NULL) {}

int MatchResult::slotOf(const std::string& name) const {
  if (_grammar == NULL) return -1;
  for (std::size_t i = 0; i < _grammar->captureCount(); ++i)
    if (_grammar->captureName(static_cast<int>(i)) == name)
      return static_cast<int>(i);
  return -1;
}

bool MatchResult::has(const std::string& name) const {
  const int slot = slotOf(name);
  if (slot < 0) return false;
  return _present[static_cast<std::size_t>(slot)] != 0;
}

const std::string& MatchResult::get(const std::string& name) const {
  const int slot = slotOf(name);
  if (slot < 0) return kEmpty;
  const std::size_t i = static_cast<std::size_t>(slot);
  return _present[i] ? _values[i] : kEmpty;
}

void MatchResult::clear() {
  _values.clear();
  _present.clear();
  _grammar = NULL;
}
