#ifndef IGRAMMARSOURCE_HPP
#define IGRAMMARSOURCE_HPP

#include <string>

class IGrammarSource {
 public:
  virtual ~IGrammarSource() {}

  virtual const char* origin() const = 0;

  virtual bool read(std::string& out) const = 0;
};

#endif
