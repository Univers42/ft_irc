#ifndef IMATCHER_HPP
#define IMATCHER_HPP

#include <string>

#include "grammar/MatchResult.hpp"

namespace Abnf {
class IMatcher {
 public:
  virtual ~IMatcher() {}

  virtual bool match(int rule, const std::string& line,
                     MatchResult& out) const = 0;

  virtual const char* strategy() const = 0;

  virtual bool lastExhausted() const = 0;
};

}  // namespace Abnf

#endif
