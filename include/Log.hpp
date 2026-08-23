#ifndef LOG_HPP
#define LOG_HPP

#include <string>

namespace Log {
enum Level { LOG_QUIET = 0, LOG_ERROR = 1, LOG_WARN = 2, LOG_INFO = 3, LOG_DEBUG = 4, LOG_TRACE = 5 };

class ILogSink {
 public:
  virtual ~ILogSink() {}
  virtual void write(char kind, const std::string& msg) = 0;

  virtual void protocol(char dir, int fd, const std::string& peer, const std::string& line, const std::string& note);
};

void setSink(ILogSink* sink);

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

void protocol(char dir, int fd, const std::string& peer, const std::string& line, const std::string& note);

}  // namespace Log

#endif
