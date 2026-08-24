#include "IrcTrace.hpp"

#include <cstddef>
#include <string>
#include <vector>

#include "ChannelModes.hpp"
#include "IrcCase.hpp"
#include "Log.hpp"
#include "Replies.hpp"
#include "libcpp/str/format.hpp"
#include "libcpp98/traffic_stats.hpp"

namespace {
struct NumericEntry {
  const char* code;
  const char* name;
};

#define FT_IRC_NUMERIC_ROW(replyName, replyCode, replyText) {replyCode, #replyName},
const NumericEntry kNumerics[] = {FT_IRC_REPLIES(FT_IRC_NUMERIC_ROW){0, 0}};
#undef FT_IRC_NUMERIC_ROW

libcpp98::TrafficStats& stats() {
  static libcpp98::TrafficStats s;
  return s;
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
  const int idx = IrcTrace::secretParamIndex(commandOf(line), paramsOf(line));
  if (idx < 0) return line;
  return redactParam(line, static_cast<size_t>(idx));
}

std::string annotate(const std::string& line) {
  std::string cmd = commandOf(line);
  if (cmd.empty()) return "";
  std::string name = IrcTrace::numericName(cmd);
  if (name.empty()) return cmd;
  return cmd + " " + name;
}

}  // namespace

int IrcTrace::secretParamIndex(const std::string& command, const std::vector<std::string>& params) {
  const std::string cmd = ircToLower(command);

  if (cmd == "pass") return 0;  //< out of range on a bare PASS: the caller redacts nothing

  if (cmd == "join") return params.size() >= 2 ? 1 : -1;  //< JOIN #a,#b key1,key2

  if (cmd == "mode") {  //< MODE #chan +k key · the key's position depends on the mode string
    if (params.size() < 2) return -1;
    size_t keyParam = 0;
    if (!ChannelModes::firstKeyParam(params[1], params.size() - 2, &keyParam)) return -1;
    return static_cast<int>(2 + keyParam);
  }

  if (cmd == RPL_CHANNELMODEIS) {  //< 324 <nick> <chan> <modes> <args> — one param further along
    if (params.size() < 3) return -1;
    size_t keyParam = 0;
    if (!ChannelModes::firstKeyParam(params[2], params.size() - 3, &keyParam)) return -1;
    return static_cast<int>(3 + keyParam);
  }

  return -1;
}

std::string IrcTrace::numericName(const std::string& numeric) {
  if (numeric.size() != 3) return "";
  for (size_t i = 0; kNumerics[i].code; ++i)
    if (numeric == kNumerics[i].code) return kNumerics[i].name;
  return "";
}

void IrcTrace::inbound(int fd, const std::string& peer, const std::string& line) {
  stats().countIn(fd, line.size() + 2);  //< +2: the CRLF framing is on the wire too

  if (!Log::enabled(Log::LOG_TRACE)) return;
  Log::protocol('<', fd, peer, redact(line), annotate(line));
}

void IrcTrace::outbound(int fd, const std::string& peer, const std::string& line) {
  stats().countOut(fd, line.size() + 2);

  if (!Log::enabled(Log::LOG_TRACE)) return;
  Log::protocol('>', fd, peer, redact(line), annotate(line));
}

void IrcTrace::sessionOpen(int fd, const std::string& host) {
  stats().open(fd);
  if (!Log::enabled(Log::LOG_DEBUG)) return;
  Log::debug() << Log::fdField(fd) << "  ++  connection from " << host;
}

void IrcTrace::sessionRegistered(int fd, const std::string& prefix) {
  if (!Log::enabled(Log::LOG_DEBUG)) return;
  Log::debug() << Log::fdField(fd) << "  ==  registered as " << prefix;
}

void IrcTrace::sessionClose(int fd, const std::string& peer, const std::string& reason) {
  const libcpp98::TrafficCounters* s = stats().get(fd);
  if (s == NULL) return;

  if (Log::enabled(Log::LOG_DEBUG)) {
    Log::debug() << Log::fdField(fd) << "  --  " << (peer.empty() ? "*" : peer) << " left (" << reason << ") — "
                 << s->linesIn << " in / " << s->linesOut << " out, " << s->bytesIn << " B / " << s->bytesOut << " B";
  }
  stats().close(fd);  //< after the read: close() drops this peer's counters
}

std::string IrcTrace::summary() {
  const libcpp98::TrafficCounters& t = stats().totals();
  return libcpp::str::to_string(stats().sessionCount()) + " session(s), " + libcpp::str::to_string(t.linesIn) +
         " lines in / " + libcpp::str::to_string(t.linesOut) + " out, " + libcpp::str::to_string(t.bytesIn) +
         " B in / " + libcpp::str::to_string(t.bytesOut) + " B out";
}
