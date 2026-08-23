#include "Message.hpp"

#include <string>
#include <vector>

#include "grammar/MatchResult.hpp"
#include "libcpp/str/format.hpp"

#include "libcpp/str/case.hpp"

Message::Message() : trailingIndex(-1), fields(NULL) {}
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
