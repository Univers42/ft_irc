#ifndef MESSAGE_HPP
#define MESSAGE_HPP

#include <cstddef>
#include <iosfwd>
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
  Message(const Message& other);
  Message& operator=(const Message& other);
  ~Message();

  bool hasTrailing() const;

  bool matched() const;

  bool has(const char* name) const;

  const std::string& field(const char* name) const;

  std::size_t count(const char* name) const;

  const std::string& field(const char* name, std::size_t index) const;

  std::vector<std::string> list(const char* name, char separator) const;

  std::vector<std::string> listKeepEmpty(const char* name, char separator) const;
};

std::ostream& operator<<(std::ostream& os, const Message& msg);

#endif
