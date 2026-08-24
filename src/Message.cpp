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

/* Was this parameter given, by whichever route the line arrived? On the
** fallback path an empty positional parameter counts as absent, because every
** production that names a required parameter types it `middle`, which is
** 1*char and so cannot match an empty one. */
bool Message::hasOr(const char* name, std::size_t index) const {
  if (fields != NULL) return has(name);
  return index < params.size() && !params[index].empty();
}

const std::string& Message::fieldOr(const char* name, std::size_t index) const {
  static const std::string kEmpty;
  if (fields != NULL) return field(name);
  return index < params.size() ? params[index] : kEmpty;
}

std::vector<std::string> Message::listOr(const char* name, std::size_t index, char separator) const {
  if (fields != NULL) return list(name, separator);
  if (index >= params.size()) return std::vector<std::string>();
  return libcpp::str::split_nonempty(params[index], separator);
}

std::vector<std::string> Message::listKeepEmptyOr(const char* name, std::size_t index, char separator) const {
  if (fields != NULL) return listKeepEmpty(name, separator);
  if (index >= params.size()) return std::vector<std::string>();
  return libcpp::str::split(params[index], separator);
}

std::vector<std::string> Message::list(const char* name, char separator) const {
  if (fields == NULL) return std::vector<std::string>();
  return fields->list(name, separator);
}

std::vector<std::string> Message::listKeepEmpty(const char* name, char separator) const {
  if (fields == NULL) return std::vector<std::string>();
  return fields->listKeepEmpty(name, separator);
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
