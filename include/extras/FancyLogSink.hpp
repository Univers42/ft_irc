#ifndef FANCYLOGSINK_HPP
#define FANCYLOGSINK_HPP

#include <string>

#include "Log.hpp"

/*
** FancyLogSink — console renderer for the full tier, drawing through
** libcpp's TermWriter (markdown-style coloured callouts + h1 banner) and
** libcpp's Srgb for the protocol trace.
**
** Installed at startup by the full tier's registerExtensions() via
** Log::setSink(new FancyLogSink). The mandatory/bonus tiers never link this
** file (nor libcpp/term) — they get Log.cpp's plain rendering, which prints
** the same columns without colour.
*/
class FancyLogSink : public Log::ILogSink {
 public:
  FancyLogSink();
  ~FancyLogSink();

  void write(char kind, const std::string& msg);

  /* Colourised protocol line. Overridden rather than inherited because the
  ** base renderer flattens everything into one string, and the whole value
  ** of a trace is being able to pick the direction, the command and the
  ** payload apart at a glance. */
  void protocol(char dir, int fd, const std::string& peer,
                const std::string& line, const std::string& note);
};

#endif /* FANCYLOGSINK_HPP */
