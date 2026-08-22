#ifndef LOG_HPP
#define LOG_HPP

#include <string>

/*
** Log — server-side console logging.
**
** The kernel ships a plain-iostream renderer (zero non-core dependencies, so
** the mandatory tier links nothing extra). A fancier renderer can be plugged
** in at startup via Log::setSink — the full tier installs FancyLogSink,
** which renders through libcpp's TermWriter (markdown-style coloured
** callouts + banner).
**
** IMPORTANT: this is for the OPERATOR CONSOLE ONLY. It must never be used to
** build client-facing data — IRC clients (HexChat, nc) require raw RFC 2812
** protocol lines, which are produced with plain string concatenation and sent
** via Client::queueMessage / Channel::broadcastMessage / Server::sendToClient.
**
** info / success / banner / debug / trace go to stdout; warn / error go to
** stderr, so a protocol trace can be piped somewhere without swallowing the
** diagnostics.
*/
namespace Log {

/* Verbosity. Each level includes everything above it.
**
**   LOG_QUIET  nothing at all
**   LOG_ERROR  fatal and refused-startup conditions
**   LOG_WARN   + recoverable problems
**   LOG_INFO   + connection lifecycle (the historical default)
**   LOG_DEBUG  + per-session detail and traffic counters
**   LOG_TRACE  + every protocol line in both directions
*/
enum Level {
  LOG_QUIET = 0,
  LOG_ERROR = 1,
  LOG_WARN = 2,
  LOG_INFO = 3,
  LOG_DEBUG = 4,
  LOG_TRACE = 5
};

/* One log line: kind is 'b'anner, 'i'nfo, 's'uccess, 'w'arn, 'e'rror,
** 'd'ebug, 't'race. */
class ILogSink {
 public:
  virtual ~ILogSink() {}
  virtual void write(char kind, const std::string& msg) = 0;

  /* One protocol line crossing the socket boundary.
  **
  ** Handed to the sink already split into its parts rather than pre-joined,
  ** so a renderer can colour the direction, the peer and the RFC annotation
  ** independently — a plain sink just concatenates them (see the default
  ** implementation in Log.cpp).
  **
  **   dir   '<' from a client, '>' towards a client
  **   fd    the socket, which is the only identity a pre-registration
  **         connection has
  **   peer  nickname if it has one, otherwise "*", matching how the
  **         protocol itself addresses an unregistered client
  **   line  the RFC 2812 line, verbatim, credentials already redacted
  **   note  the command, plus its RFC name when it is a numeric */
  virtual void protocol(char dir, int fd, const std::string& peer,
                        const std::string& line, const std::string& note);
};

/* Install a renderer (Log takes ownership; pass NULL to restore the
** plain fallback). */
void setSink(ILogSink* sink);

/* Verbosity control. configureFromEnv() reads FT_IRC_LOG, accepting a level
** name (quiet|error|warn|info|debug|trace) or a digit 0-5; anything else
** leaves the default untouched. It is read from the environment rather than
** the config file because the config file is a full-tier feature and the
** trace has to be available on the mandatory binary, which is the one that
** gets defended. */
void setLevel(Level level);
Level level();
bool enabled(Level required);
void configureFromEnv();

void banner(const std::string& title);
void info(const std::string& msg);
void success(const std::string& msg);
void warn(const std::string& msg);
void error(const std::string& msg);
void debug(const std::string& msg);
void trace(const std::string& msg);

/* Protocol trace passthrough — normally reached through IrcTrace, which adds
** redaction, RFC annotation and counters. */
void protocol(char dir, int fd, const std::string& peer,
              const std::string& line, const std::string& note);

}  // namespace Log

#endif /* LOG_HPP */
