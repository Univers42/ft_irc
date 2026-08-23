#ifndef MESSAGE_HPP
#define MESSAGE_HPP

#include <cstddef>
#include <string>
#include <vector>

namespace Abnf {
class MatchResult;
}

struct Message {
  std::string command;
  std::vector<std::string> params;
  int trailingIndex;
  const Abnf::MatchResult* fields;

  Message();

  bool hasTrailing() const;

  bool matched() const;

  bool has(const char* name) const;

  const std::string& field(const char* name) const;

  std::size_t count(const char* name) const;

  const std::string& field(const char* name, std::size_t index) const;

  std::vector<std::string> list(const char* name, char separator) const;

  std::vector<std::string> listKeepEmpty(const char* name, char separator) const;
};

#endif
