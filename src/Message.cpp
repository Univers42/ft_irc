#include "Message.hpp"

#include <ostream>
#include <string>
#include <vector>

#include "grammar/MatchResult.hpp"
#include "libcpp/str/format.hpp"

#include "libcpp/str/case.hpp"

Message::Message() : trailingIndex(-1), fields(NULL) {}

Message::Message(const Message& other)
    : command(other.command), params(other.params), trailingIndex(other.trailingIndex), fields(other.fields) {}

Message& Message::operator=(const Message& other) {
  if (this != &other) {
    command = other.command;
    params = other.params;
    trailingIndex = other.trailingIndex;
    fields = other.fields;
  }
  return *this;
}

Message::~Message() {}

bool Message::hasTrailing() const { return trailingIndex >= 0; }
bool Message::matched() const { return fields != NULL; }
bool Message::has(const char* name) const { return fields != NULL && fields->has(name); }
const std::string& Message::field(const char* name) const { return field(name, 0); }

std::size_t Message::count(const char* name) const {
  if (fields == NULL) return 0;
  return fields->count(name);
}

const std::string& Message::field(const char* name, std::size_t index) const {
  static const std::string kEmpty;
  if (fields == NULL) return kEmpty;
  return fields->at(name, index);
}

std::vector<std::string> Message::list(const char* name, char separator) const {
  if (fields == NULL) return std::vector<std::string>();
  return libcpp::str::split_nonempty(fields->at(name, 0), separator);
}

std::vector<std::string> Message::listKeepEmpty(const char* name, char separator) const {
  if (fields == NULL) return std::vector<std::string>();
  return libcpp::str::split(fields->at(name, 0), separator);
}

std::ostream& operator<<(std::ostream& os, const Message& msg) {
  os << (msg.command.empty() ? "(none)" : msg.command);
  for (std::size_t i = 0; i < msg.params.size(); ++i) {
    const bool trailing = msg.trailingIndex >= 0 && static_cast<std::size_t>(msg.trailingIndex) == i;
    os << ' ';
    if (trailing) os << ':';
    os << msg.params[i];
  }
  if (msg.fields == NULL) os << " (unmatched)";
  return os;
}
