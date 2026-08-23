#include "Log.hpp"

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <string>

#include "libcpp/str/format.hpp"

namespace {
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

  if (std::strcmp(v, "quiet") == 0 || std::strcmp(v, "0") == 0)
    g_level = LOG_QUIET;
  else if (std::strcmp(v, "error") == 0 || std::strcmp(v, "1") == 0)
    g_level = LOG_ERROR;
  else if (std::strcmp(v, "warn") == 0 || std::strcmp(v, "2") == 0)
    g_level = LOG_WARN;
  else if (std::strcmp(v, "info") == 0 || std::strcmp(v, "3") == 0)
    g_level = LOG_INFO;
  else if (std::strcmp(v, "debug") == 0 || std::strcmp(v, "4") == 0)
    g_level = LOG_DEBUG;
  else if (std::strcmp(v, "trace") == 0 || std::strcmp(v, "5") == 0)
    g_level = LOG_TRACE;
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
