#ifndef LOG_HPP
#define LOG_HPP

#include <string>

#include "libcpp/log/stream.hpp"

namespace Log {
enum Level { LOG_QUIET = 0, LOG_ERROR = 1, LOG_WARN = 2, LOG_INFO = 3, LOG_DEBUG = 4, LOG_TRACE = 5 };

class ILogSink {
 public:
  virtual ~ILogSink() {}
  virtual void write(char kind, const std::string& msg) = 0;

  virtual void protocol(char dir, int fd, const std::string& peer, const std::string& line, const std::string& note);

 protected:
  ILogSink() {}
  ILogSink(const ILogSink& other) { (void)other; }
  ILogSink& operator=(const ILogSink& other) {
    (void)other;
    return *this;
  }
};

/*
** The message builder is libcpp's BasicStream, tagged by our `kind` char
** rather than by a severity enum. That is the whole reason BasicStream takes
** the tag as a template parameter: 'b'anner and 's'uccess are presentation
** intents, and no ordering of severities can express them.
**
** Still lazy — a suppressed trace allocates nothing. See libcpp/log/stream.hpp
** for the copy-is-a-move caveat: use these as temporaries, not named locals.
*/
typedef libcpp::log::BasicStream<char> Stream;

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

Stream banner();
Stream info();
Stream success();
Stream warn();
Stream error();
Stream debug();
Stream trace();

void protocol(char dir, int fd, const std::string& peer, const std::string& line, const std::string& note);

/* "fd   7" — the fixed-width descriptor column every protocol and session
** line starts with. One definition, so the three renderers cannot drift. */
std::string fdField(int fd);

}  // namespace Log

#endif
