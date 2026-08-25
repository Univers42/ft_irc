#include "grammar/MatchResult.hpp"

#include <ostream>
#include <string>
#include <vector>

#include "grammar/Grammar.hpp"
#include "libcpp/str/format.hpp"

/*
** The end of the pipeline: what a successful match hands back.
**
** The same captures are stored TWICE, because two callers want two shapes:
**
**   _values    one bucket per capture slot, each holding every value that
**              capture grabbed. This is the by-name view -- get("chanlist").
**              Command handlers use it.
**   _sequence  every captured value flattened into one list, in the order the
**              LINE produced them, with _owners recording which slot each came
**              from. Server::fillParams() needs this: IRC parameters are
**              positional, so "MODE #c +ov alice bob" has to keep alice before
**              bob even though both landed in the same $modeparam slot.
**
** Neither view is derivable from the other cheaply, which is why both are kept.
**
** Every by-name accessor is TOTAL: an unknown name or an out-of-range index
** gives "" rather than undefined behaviour, so a handler can probe without
** guarding first. has() is there for when "absent" and "empty" must differ.
*/
namespace Abnf {
namespace {
const std::string& emptyString() {
  static const std::string kEmpty;
  return kEmpty;
}

}  // namespace

MatchResult::MatchResult() : _grammar(NULL) {}

MatchResult::MatchResult(const MatchResult& other)
    : _grammar(other._grammar), _values(other._values), _sequence(other._sequence), _owners(other._owners) {}

MatchResult& MatchResult::operator=(const MatchResult& other) {
  if (this != &other) {
    _grammar = other._grammar;
    _values = other._values;
    _sequence = other._sequence;
    _owners = other._owners;
  }
  return *this;
}

MatchResult::~MatchResult() {}

//< Every by-name accessor funnels through here. Linear over the capture names
//< and case-SENSITIVE, matching Grammar::captureIndex(). Unbound (_grammar
//< NULL) answers -1, which is what makes a default-constructed result inert.
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

std::size_t MatchResult::sequenceSize() const { return _sequence.size(); }

const std::string& MatchResult::sequenceAt(std::size_t index) const {
  if (index >= _sequence.size()) return emptyString();
  return _sequence[index];
}

int MatchResult::sequenceOwner(std::size_t index) const {
  if (index >= _owners.size()) return -1;
  return _owners[index];
}

//< swap(), not assign: the matcher built these in its own scratch space and is
//< done with them, so moving the buffers across costs nothing. That is also why
//< the parameters are non-const references -- the caller's vectors come back
//< empty, by design.
void MatchResult::adoptSequence(std::vector<std::string>& sequence, std::vector<int>& owners) {
  _sequence.swap(sequence);
  _owners.swap(owners);
}

std::vector<std::string> MatchResult::list(const std::string& name, char separator) const {
  return libcpp::str::split_nonempty(at(name, 0), separator);
}

std::vector<std::string> MatchResult::listKeepEmpty(const std::string& name, char separator) const {
  return libcpp::str::split(at(name, 0), separator);
}

void MatchResult::clear() {
  _sequence.clear();
  _owners.clear();
  _values.clear();
  _grammar = NULL;
}

//< Called at the top of every IMatcher::match(), which is what lets a caller
//< hand the same MatchResult to call after call. Sizing _values to
//< captureCount() here is what adopt() then relies on.
void MatchResult::reset(const Grammar& grammar) {
  _grammar = &grammar;
  _sequence.clear();
  _owners.clear();
  _values.assign(grammar.captureCount(), std::vector<std::string>());
}

void MatchResult::adopt(std::vector<std::vector<std::string> >& values) { _values.swap(values); }

//< Debug dump. Walks _values rather than _sequence so each value prints with
//< its capture NAME, which is the useful thing when reading a trace.
std::ostream& operator<<(std::ostream& os, const MatchResult& result) {
  if (result._grammar == NULL) {  //< never reset() -- no names to resolve against
    os << "{unbound}";
    return os;
  }
  os << "{";
  bool first = true;
  for (std::size_t slot = 0; slot < result._values.size(); ++slot) {
    const std::vector<std::string>& values = result._values[slot];
    for (std::size_t i = 0; i < values.size(); ++i) {
      if (!first) os << ", ";
      first = false;
      os << result._grammar->captureName(static_cast<int>(slot)) << "=" << values[i];
    }
  }
  os << "}";
  return os;
}

}  // namespace Abnf
