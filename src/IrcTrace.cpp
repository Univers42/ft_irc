#include "IrcTrace.hpp"

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "ChannelModes.hpp"
#include "IrcCase.hpp"
#include "Log.hpp"
#include "Replies.hpp"
#include "libcpp/str/format.hpp"

namespace {
struct NumericEntry {
  const char* code;
  const char* name;
};

#define FT_IRC_NUMERIC_ROW(replyName, replyCode, replyText) {replyCode, #replyName},
const NumericEntry kNumerics[] = {FT_IRC_REPLIES(FT_IRC_NUMERIC_ROW){0, 0}};
#undef FT_IRC_NUMERIC_ROW

struct Stats {
  unsigned long linesIn;
  unsigned long linesOut;
  unsigned long bytesIn;
  unsigned long bytesOut;
  Stats() : linesIn(0), linesOut(0), bytesIn(0), bytesOut(0) {}
};

std::map<int, Stats>& sessions() {
  static std::map<int, Stats> s;
  return s;
}

Stats& total() {
  static Stats t;
  return t;
}

unsigned long& sessionCount() {
  static unsigned long n = 0;
  return n;
}

std::string commandOf(const std::string& line) {
  std::string::size_type pos = 0;
  while (pos < line.size() && line[pos] == ' ') ++pos;
  if (pos < line.size() &&
      line[pos] == ':') {  //< leading ':' = a PREFIX · ":srv 001 nick :hi" · clients rarely send one
    std::string::size_type sp = line.find(' ', pos);
    if (sp == std::string::npos) return "";
    pos = sp;
    while (pos < line.size() && line[pos] == ' ') ++pos;
  }
  std::string::size_type end = line.find(' ', pos);
  if (end == std::string::npos) return line.substr(pos);
  return line.substr(pos, end - pos);
}

std::string redactParam(const std::string& line, size_t idx) {
  std::string out;
  std::string::size_type pos = 0;
  size_t field = 0;
  bool sawPrefix = false;
  bool sawCommand = false;

  while (pos <= line.size()) {
    std::string::size_type sp = line.find(' ', pos);
    std::string tok = (sp == std::string::npos) ? line.substr(pos) : line.substr(pos, sp - pos);

    if (!tok.empty()) {
      if (!sawPrefix && !sawCommand && tok[0] == ':') {  //< only the FIRST token · a later ':' opens the trailing
        sawPrefix = true;
      } else if (!sawCommand) {
        sawCommand = true;
      } else {
        if (field == idx) tok = "***";
        ++field;
      }
    }
    out += tok;
    if (sp == std::string::npos) break;
    out += ' ';
    pos = sp + 1;
  }
  return out;
}

std::vector<std::string> paramsOf(const std::string& line) {
  std::vector<std::string> all = libcpp::str::split_nonempty(line, ' ');
  std::vector<std::string> out;
  size_t i = 0;
  if (i < all.size() && !all[i].empty() && all[i][0] == ':') ++i;
  if (i < all.size()) ++i;
  for (; i < all.size(); ++i) out.push_back(all[i]);
  return out;
}

std::string redact(const std::string& line) {
  std::string cmd = ircToLower(commandOf(line));

  if (cmd == "pass") return redactParam(line, 0);

  if (cmd == "join") {
    if (paramsOf(line).size() >= 2) return redactParam(line, 1);
    return line;
  }

  if (cmd == "mode") {
    std::vector<std::string> params = paramsOf(line);
    if (params.size() < 2) return line;

    const std::string& modeStr = params[1];
    size_t keyParam = 0;
    if (!ChannelModes::firstKeyParam(modeStr, params.size() - 2, &keyParam)) return line;

    return redactParam(line, 2 + keyParam);
  }

  if (cmd == RPL_CHANNELMODEIS) {
    std::vector<std::string> params = paramsOf(line);
    if (params.size() < 3) return line;
    const std::string& modeStr = params[2];
    size_t keyParam = 0;
    if (!ChannelModes::firstKeyParam(modeStr, params.size() - 3, &keyParam)) return line;

    return redactParam(line, 3 + keyParam);
  }

  return line;
}

std::string fdField(int fd) { return "fd " + libcpp::str::pad_left(libcpp::str::to_string(fd), 3, ' '); }

std::string annotate(const std::string& line) {
  std::string cmd = commandOf(line);
  if (cmd.empty()) return "";
  std::string name = IrcTrace::numericName(cmd);
  if (name.empty()) return cmd;
  return cmd + " " + name;
}

}  // namespace

std::string IrcTrace::numericName(const std::string& numeric) {
  if (numeric.size() != 3) return "";
  for (size_t i = 0; kNumerics[i].code; ++i)
    if (numeric == kNumerics[i].code) return kNumerics[i].name;
  return "";
}

void IrcTrace::inbound(int fd, const std::string& peer, const std::string& line) {
  Stats& s = sessions()[fd];
  ++s.linesIn;
  s.bytesIn += line.size() + 2;
  ++total().linesIn;
  total().bytesIn += line.size() + 2;

  if (!Log::enabled(Log::LOG_TRACE)) return;
  Log::protocol('<', fd, peer, redact(line), annotate(line));
}

void IrcTrace::outbound(int fd, const std::string& peer, const std::string& line) {
  Stats& s = sessions()[fd];
  ++s.linesOut;
  s.bytesOut += line.size() + 2;
  ++total().linesOut;
  total().bytesOut += line.size() + 2;

  if (!Log::enabled(Log::LOG_TRACE)) return;
  Log::protocol('>', fd, peer, redact(line), annotate(line));
}

void IrcTrace::sessionOpen(int fd, const std::string& host) {
  sessions()[fd] = Stats();
  ++sessionCount();
  if (!Log::enabled(Log::LOG_DEBUG)) return;
  Log::debug() << fdField(fd) << "  ++  connection from " << host;
}

void IrcTrace::sessionRegistered(int fd, const std::string& prefix) {
  if (!Log::enabled(Log::LOG_DEBUG)) return;
  Log::debug() << fdField(fd) << "  ==  registered as " << prefix;
}

void IrcTrace::sessionClose(int fd, const std::string& peer, const std::string& reason) {
  std::map<int, Stats>::iterator it = sessions().find(fd);
  if (it != sessions().end()) {
    if (Log::enabled(Log::LOG_DEBUG)) {
      const Stats& s = it->second;
      Log::debug() << fdField(fd) << "  --  " << (peer.empty() ? "*" : peer) << " left (" << reason << ") — "
                   << s.linesIn << " in / " << s.linesOut << " out, " << s.bytesIn << " B / " << s.bytesOut << " B";
    }

    sessions().erase(it);
  }
}

std::string IrcTrace::summary() {
  const Stats& t = total();
  return libcpp::str::to_string(sessionCount()) + " session(s), " + libcpp::str::to_string(t.linesIn) + " lines in / " +
         libcpp::str::to_string(t.linesOut) + " out, " + libcpp::str::to_string(t.bytesIn) + " B in / " +
         libcpp::str::to_string(t.bytesOut) + " B out";
}
