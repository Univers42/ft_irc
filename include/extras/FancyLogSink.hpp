#ifndef FANCYLOGSINK_HPP
#define FANCYLOGSINK_HPP

#include <string>

#include "Log.hpp"

class FancyLogSink : public Log::ILogSink {
 public:
  FancyLogSink();
  ~FancyLogSink();

  void write(char kind, const std::string& msg);

  void protocol(char dir, int fd, const std::string& peer, const std::string& line, const std::string& note);

 private:
  FancyLogSink(const FancyLogSink& other);
  FancyLogSink& operator=(const FancyLogSink& other);
};

#endif
