#ifndef MESSAGE_HPP
#define MESSAGE_HPP

#include <string>
#include <vector>

struct Message {
  std::string command;
  std::vector<std::string> params;
  int trailingIndex;

  Message();

  bool hasTrailing() const;

  static Message parse(const std::string& raw);
};

#endif
