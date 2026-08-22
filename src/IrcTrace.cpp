#include "IrcTrace.hpp"

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "IrcCase.hpp"
#include "Log.hpp"
#include "Replies.hpp"
#include "libcpp/str/format.hpp"

namespace {

/* ─── Numeric → RFC name ───
**
** Exactly the numerics this server sends, mirroring Replies.hpp. The table is
** built from those macros rather than re-typed, so a numeric can never drift
** between what is sent and what the log calls it. Replies.hpp is deliberately
** an inventory of what the server actually emits, which makes it the right
** and only source for this list. */
struct NumericEntry {
  const char* code;
  const char* name;
};

const NumericEntry kNumerics[] = {
    {RPL_WELCOME, "RPL_WELCOME"},
    {RPL_YOURHOST, "RPL_YOURHOST"},
    {RPL_CREATED, "RPL_CREATED"},
    {RPL_MYINFO, "RPL_MYINFO"},
    {RPL_ISUPPORT, "RPL_ISUPPORT"},
    {RPL_UMODEIS, "RPL_UMODEIS"},
    {RPL_USERHOST, "RPL_USERHOST"},
    {RPL_WHOISUSER, "RPL_WHOISUSER"},
    {RPL_WHOISSERVER, "RPL_WHOISSERVER"},
    {RPL_ENDOFWHOIS, "RPL_ENDOFWHOIS"},
    {RPL_WHOISCHANNELS, "RPL_WHOISCHANNELS"},
    {RPL_CHANNELMODEIS, "RPL_CHANNELMODEIS"},
    {RPL_CREATIONTIME, "RPL_CREATIONTIME"},
    {RPL_NOTOPIC, "RPL_NOTOPIC"},
    {RPL_TOPIC, "RPL_TOPIC"},
    {RPL_TOPICWHOTIME, "RPL_TOPICWHOTIME"},
    {RPL_INVITING, "RPL_INVITING"},
    {RPL_WHOREPLY, "RPL_WHOREPLY"},
    {RPL_ENDOFWHO, "RPL_ENDOFWHO"},
    {RPL_NAMREPLY, "RPL_NAMREPLY"},
    {RPL_ENDOFNAMES, "RPL_ENDOFNAMES"},
    {ERR_NOSUCHNICK, "ERR_NOSUCHNICK"},
    {ERR_NOSUCHCHANNEL, "ERR_NOSUCHCHANNEL"},
    {ERR_CANNOTSENDTOCHAN, "ERR_CANNOTSENDTOCHAN"},
    {ERR_NORECIPIENT, "ERR_NORECIPIENT"},
    {ERR_NOTEXTTOSEND, "ERR_NOTEXTTOSEND"},
    {ERR_UNKNOWNCOMMAND, "ERR_UNKNOWNCOMMAND"},
    {ERR_NOMOTD, "ERR_NOMOTD"},
    {ERR_NONICKNAMEGIVEN, "ERR_NONICKNAMEGIVEN"},
    {ERR_ERRONEUSNICKNAME, "ERR_ERRONEUSNICKNAME"},
    {ERR_NICKNAMEINUSE, "ERR_NICKNAMEINUSE"},
    {ERR_USERNOTINCHANNEL, "ERR_USERNOTINCHANNEL"},
    {ERR_NOTONCHANNEL, "ERR_NOTONCHANNEL"},
    {ERR_USERONCHANNEL, "ERR_USERONCHANNEL"},
    {ERR_NOTREGISTERED, "ERR_NOTREGISTERED"},
    {ERR_NEEDMOREPARAMS, "ERR_NEEDMOREPARAMS"},
    {ERR_ALREADYREGISTRED, "ERR_ALREADYREGISTRED"},
    {ERR_PASSWDMISMATCH, "ERR_PASSWDMISMATCH"},
    {ERR_CHANNELISFULL, "ERR_CHANNELISFULL"},
    {ERR_UNKNOWNMODE, "ERR_UNKNOWNMODE"},
    {ERR_INVITEONLYCHAN, "ERR_INVITEONLYCHAN"},
    {ERR_BADCHANNELKEY, "ERR_BADCHANNELKEY"},
    {ERR_BADCHANMASK, "ERR_BADCHANMASK"},
    {ERR_CHANOPRIVSNEEDED, "ERR_CHANOPRIVSNEEDED"},
    {ERR_USERSDONTMATCH, "ERR_USERSDONTMATCH"},
    {ERR_INVALIDKEY, "ERR_INVALIDKEY"},
    {ERR_INVALIDMODEPARAM, "ERR_INVALIDMODEPARAM"},
    {0, 0}};

/* ─── Session and global counters ─── */
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

/* The first word of a line, skipping an optional ":prefix ". Mirrors
** Message::parse's own prefix handling so the label the log prints is the
** command the dispatcher will actually see. */
std::string commandOf(const std::string& line) {
  std::string::size_type pos = 0;
  while (pos < line.size() && line[pos] == ' ') ++pos;
  if (pos < line.size() && line[pos] == ':') {
    std::string::size_type sp = line.find(' ', pos);
    if (sp == std::string::npos) return "";
    pos = sp;
    while (pos < line.size() && line[pos] == ' ') ++pos;
  }
  std::string::size_type end = line.find(' ', pos);
  if (end == std::string::npos) return line.substr(pos);
  return line.substr(pos, end - pos);
}

/* Replace parameter number `idx` (0-based, after the command) with ***.
** Operates on the raw line so the redacted form is still a valid IRC line —
** the log is meant to be re-readable as protocol, not as prose.
**
** An optional leading ":prefix" is skipped, not counted. Outbound lines carry
** one and inbound lines normally do not, so treating the prefix as the
** command shifts every field by one: the server's own JOIN echo had its
** CHANNEL starred out instead of a key, and a MODE broadcast starred the
** "+k" rather than the secret after it — redacting the wrong token while
** still printing the credential. */
std::string redactParam(const std::string& line, size_t idx) {
  std::string out;
  std::string::size_type pos = 0;
  size_t field = 0;
  bool sawPrefix = false;
  bool sawCommand = false;

  while (pos <= line.size()) {
    std::string::size_type sp = line.find(' ', pos);
    std::string tok =
        (sp == std::string::npos) ? line.substr(pos) : line.substr(pos, sp - pos);

    if (!tok.empty()) {
      if (!sawPrefix && !sawCommand && tok[0] == ':') {
        sawPrefix = true; /* a prefix, not the command */
      } else if (!sawCommand) {
        sawCommand = true; /* this token is the command */
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

/* The line's parameters, prefix and command excluded. */
std::vector<std::string> paramsOf(const std::string& line) {
  std::vector<std::string> all = libcpp::str::split_nonempty(line, ' ');
  std::vector<std::string> out;
  size_t i = 0;
  if (i < all.size() && !all[i].empty() && all[i][0] == ':') ++i; /* prefix */
  if (i < all.size()) ++i;                                        /* command */
  for (; i < all.size(); ++i) out.push_back(all[i]);
  return out;
}

/* Credentials never reach the console. A trace that cannot be pasted into a
** bug report is a trace nobody uses. */
std::string redact(const std::string& line) {
  std::string cmd = ircToLower(commandOf(line));

  if (cmd == "pass") return redactParam(line, 0);

  /* JOIN <channels> <keys> — the second parameter is a key list. The
  ** server's own JOIN echo has no key, so this only ever fires inbound. */
  if (cmd == "join") {
    if (paramsOf(line).size() >= 2) return redactParam(line, 1);
    return line;
  }

  /* MODE <target> <modestring> [params] — redact a key argument. The
  ** parameter index of the key depends on where 'k' sits among the
  ** parameter-taking letters, so walk the mode string the way
  ** handleChannelMode does rather than guessing a fixed position. */
  if (cmd == "mode") {
    std::vector<std::string> params = paramsOf(line);
    if (params.size() < 2) return line;

    const std::string& modeStr = params[1];
    bool adding = true;
    size_t paramNo = 0;   /* index among parameters after the mode string */
    size_t keyParam = 0;
    bool found = false;
    for (size_t i = 0; i < modeStr.size(); ++i) {
      char c = modeStr[i];
      if (c == '+') {
        adding = true;
      } else if (c == '-') {
        adding = false;
      } else if (c == 'k') {
        if (adding && !found) {
          keyParam = paramNo;
          found = true;
        }
        ++paramNo;
      } else if (c == 'o' || (c == 'l' && adding)) {
        ++paramNo;
      }
    }
    if (!found) return line;
    /* Parameter 0 is the target and 1 is the mode string, so the arguments
    ** the mode letters consume start at 2. */
    return redactParam(line, 2 + keyParam);
  }

  /* RPL_CHANNELMODEIS (324) reports the channel's modes back to a member,
  ** and carries the +k key as a parameter -- the very reason the MODE query
  ** path requires membership. It is the one *reply* that contains a
  ** credential, so it is redacted like the commands above.
  **
  **   :ft_irc 324 bob #k +k topsecret   ->   :ft_irc 324 bob #k +k ***
  **
  ** Parameters here are <nick> <channel> <modestring> [args], so the mode
  ** arguments start at index 3 rather than 2. */
  if (cmd == RPL_CHANNELMODEIS) {
    std::vector<std::string> params = paramsOf(line);
    if (params.size() < 3) return line;
    const std::string& modeStr = params[2];

    bool adding = true;
    size_t paramNo = 0;
    size_t keyParam = 0;
    bool found = false;
    for (size_t i = 0; i < modeStr.size(); ++i) {
      char c = modeStr[i];
      if (c == '+') {
        adding = true;
      } else if (c == '-') {
        adding = false;
      } else if (c == 'k') {
        if (adding && !found) {
          keyParam = paramNo;
          found = true;
        }
        ++paramNo;
      } else if (c == 'o' || (c == 'l' && adding)) {
        ++paramNo;
      }
    }
    if (!found) return line;
    return redactParam(line, 3 + keyParam);
  }

  return line;
}

/* "  fd 7 " — right-aligned so columns line up as fds grow past 9. */
std::string fdField(int fd) {
  return "fd " + libcpp::str::pad_left(libcpp::str::to_string(fd), 3, ' ');
}

/* The annotation printed after the line: the command, plus the RFC name when
** it is a numeric. This is what turns ":ft_irc 462 bob :You may not
** reregister" from a string into a diagnosis. */
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

void IrcTrace::inbound(int fd, const std::string& peer,
                       const std::string& line) {
  Stats& s = sessions()[fd];
  ++s.linesIn;
  s.bytesIn += line.size() + 2; /* the CRLF is on the wire too */
  ++total().linesIn;
  total().bytesIn += line.size() + 2;

  if (!Log::enabled(Log::LOG_TRACE)) return;
  Log::protocol('<', fd, peer, redact(line), annotate(line));
}

void IrcTrace::outbound(int fd, const std::string& peer,
                        const std::string& line) {
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
  Log::debug(fdField(fd) + "  ++  connection from " + host);
}

void IrcTrace::sessionRegistered(int fd, const std::string& prefix) {
  if (!Log::enabled(Log::LOG_DEBUG)) return;
  Log::debug(fdField(fd) + "  ==  registered as " + prefix);
}

void IrcTrace::sessionClose(int fd, const std::string& peer,
                            const std::string& reason) {
  std::map<int, Stats>::iterator it = sessions().find(fd);
  if (it != sessions().end()) {
    if (Log::enabled(Log::LOG_DEBUG)) {
      const Stats& s = it->second;
      Log::debug(fdField(fd) + "  --  " + (peer.empty() ? "*" : peer) + " left (" +
                 reason + ") — " + libcpp::str::to_string(s.linesIn) + " in / " +
                 libcpp::str::to_string(s.linesOut) + " out, " +
                 libcpp::str::to_string(s.bytesIn) + " B / " +
                 libcpp::str::to_string(s.bytesOut) + " B");
    }
    /* Erased on every close, including the abortive ones: fds are recycled,
    ** and a stale entry would credit the next client with this one's
    ** traffic. */
    sessions().erase(it);
  }
}

std::string IrcTrace::summary() {
  const Stats& t = total();
  return libcpp::str::to_string(sessionCount()) + " session(s), " +
         libcpp::str::to_string(t.linesIn) + " lines in / " +
         libcpp::str::to_string(t.linesOut) + " out, " +
         libcpp::str::to_string(t.bytesIn) + " B in / " +
         libcpp::str::to_string(t.bytesOut) + " B out";
}
