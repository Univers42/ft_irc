#include "IrcCase.hpp"

#include <string>

#include "libcpp/str/case.hpp"

std::string ircToLower(const std::string& s) {
  return libcpp::str::ascii_to_lower(s);
}

bool ircEquals(const std::string& a, const std::string& b) {
  return libcpp::str::eq_ascii_nocase(a, b);
}
