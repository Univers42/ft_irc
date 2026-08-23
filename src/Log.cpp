#include "Log.hpp"

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <string>

#include "Dispatch.hpp"
#include "libcpp/str/format.hpp"

namespace {
struct LevelName {
  const char* name;
  Log::Level level;
};

const LevelName kLevelNames[] = {
    {"quiet", Log::LOG_QUIET}, {"0", Log::LOG_QUIET}, {"error", Log::LOG_ERROR}, {"1", Log::LOG_ERROR},
    {"warn", Log::LOG_WARN},   {"2", Log::LOG_WARN},  {"info", Log::LOG_INFO},   {"3", Log::LOG_INFO},
    {"debug", Log::LOG_DEBUG}, {"4", Log::LOG_DEBUG}, {"trace", Log::LOG_TRACE}, {"5", Log::LOG_TRACE},
    {NULL, Log::LOG_QUIET},
};

Log::ILogSink* g_sink = 0;
Log::Level g_level = Log::LOG_INFO;

std::string stamp() {
  std::time_t now = std::time(NULL);
  std::tm* lt = std::localtime(&now);
  char buf[16];
  if (!lt || std::strftime(buf, sizeof(buf), "%H:%M:%S", lt) == 0) return "--:--:--";
  return std::string(buf);
}

void fallback(char kind, const std::string& msg) {
  switch (kind) {
    case 'b':
      std::cout << "== " << msg << " ==" << std::endl;
      break;
    case 'i':
      std::cout << "[ircserv] info: " << msg << std::endl;
      break;
    case 's':
      std::cout << "[ircserv] ok:   " << msg << std::endl;
      break;
    case 'w':
      std::cerr << "[ircserv] warn: " << msg << std::endl;
      break;
    case 'e':
      std::cerr << "[ircserv] error: " << msg << std::endl;
      break;
    case 'd':
      std::cout << "[ircserv] " << stamp() << " " << msg << std::endl;
      break;
    case 't':
      std::cout << "[ircserv] " << stamp() << " " << msg << std::endl;
      break;
  }
}

void render(char kind, const std::string& msg) {
  if (g_sink)
    g_sink->write(kind, msg);
  else
    fallback(kind, msg);
}
}  // namespace

void Log::ILogSink::protocol(char dir, int fd, const std::string& peer, const std::string& line,
                             const std::string& note) {
  std::string arrow = (dir == '<') ? "<<" : ">>";
  std::string who = peer.empty() ? "*" : peer;

  std::string out = "fd " + libcpp::str::pad_left(libcpp::str::to_string(fd), 3, ' ') + "  " + arrow + "  " +
                    libcpp::str::pad_right(who, 9, ' ') + "  " + line;
  if (!note.empty()) out += "   [" + note + "]";
  write('t', out);
}

void Log::setSink(ILogSink* sink) {
  delete g_sink;
  g_sink = sink;
}

void Log::setLevel(Level level) { g_level = level; }
Log::Level Log::level() { return g_level; }
bool Log::enabled(Level required) { return g_level >= required; }

void Log::configureFromEnv() {
  const char* v = std::getenv("FT_IRC_LOG");
  if (!v || !*v) return;

  const LevelName* entry = Dispatch::find(kLevelNames, std::string(v));
  if (entry != NULL) g_level = entry->level;
}

void Log::banner(const std::string& title) {
  if (enabled(LOG_ERROR)) render('b', title);
}

void Log::info(const std::string& msg) {
  if (enabled(LOG_INFO)) render('i', msg);
}

void Log::success(const std::string& msg) {
  if (enabled(LOG_INFO)) render('s', msg);
}

void Log::warn(const std::string& msg) {
  if (enabled(LOG_WARN)) render('w', msg);
}

void Log::error(const std::string& msg) {
  if (enabled(LOG_ERROR)) render('e', msg);
}

void Log::debug(const std::string& msg) {
  if (enabled(LOG_DEBUG)) render('d', msg);
}

void Log::trace(const std::string& msg) {
  if (enabled(LOG_TRACE)) render('t', msg);
}

void Log::protocol(char dir, int fd, const std::string& peer, const std::string& line, const std::string& note) {
  if (!enabled(LOG_TRACE)) return;
  if (g_sink) {
    g_sink->protocol(dir, fd, peer, line, note);
    return;
  }

  struct PlainSink : public ILogSink {
    void write(char kind, const std::string& msg) { fallback(kind, msg); }
  };
  static PlainSink plain;
  plain.protocol(dir, fd, peer, line, note);
}
