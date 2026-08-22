#include "Message.hpp"

#include <string>

#include "libcpp/str/case.hpp"

Message::Message() : trailingIndex(-1) {}

bool Message::hasTrailing() const { return trailingIndex >= 0; }

Message Message::parse(const std::string& raw) {
  Message msg;
  std::string line = raw;
  std::string::size_type pos = 0;

  while (pos < line.size() && line[pos] == ' ') ++pos;

  if (pos < line.size() && line[pos] == ':') {
    std::string::size_type end = line.find(' ', pos);
    if (end == std::string::npos) return msg;
    pos = end;
    while (pos < line.size() && line[pos] == ' ') ++pos;
  }

  if (pos < line.size()) {
    std::string::size_type end = line.find(' ', pos);
    if (end == std::string::npos) {
      msg.command = line.substr(pos);
    } else {
      msg.command = line.substr(pos, end - pos);
      pos = end;
    }

    msg.command = libcpp::str::to_upper(msg.command);

    if (end == std::string::npos) return msg;

    while (pos < line.size() && line[pos] == ' ') ++pos;
  }

  while (pos < line.size()) {
    if (line[pos] == ':') {
      msg.params.push_back(line.substr(pos + 1));
      break;
    }
    std::string::size_type end = line.find(' ', pos);
    if (end == std::string::npos) {
      msg.params.push_back(line.substr(pos));
      break;
    }
    msg.params.push_back(line.substr(pos, end - pos));
    pos = end;
    while (pos < line.size() && line[pos] == ' ') ++pos;
  }

  return msg;
}
