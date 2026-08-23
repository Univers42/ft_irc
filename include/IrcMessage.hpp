#ifndef IRCMESSAGE_HPP
#define IRCMESSAGE_HPP

#include <string>

namespace IrcMessage {

inline std::string relay(const std::string& prefix, const char* command, const std::string& params) {
  std::string out = ":";
  out += prefix;
  out += ' ';
  out += command;
  if (!params.empty()) {
    out += ' ';
    out += params;
  }
  return out;
}

inline std::string relay(const std::string& prefix, const char* command, const std::string& params,
                         const std::string& trailing) {
  return relay(prefix, command, params) + " :" + trailing;
}

}  // namespace IrcMessage

#endif
