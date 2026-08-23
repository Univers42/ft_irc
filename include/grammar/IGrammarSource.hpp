#ifndef IGRAMMARSOURCE_HPP
#define IGRAMMARSOURCE_HPP

#include <string>

namespace Abnf {
class IGrammarSource {
 public:
  virtual ~IGrammarSource() {}

  virtual const char* origin() const = 0;

  virtual bool read(std::string& out) const = 0;

 protected:
  IGrammarSource() {}
  IGrammarSource(const IGrammarSource& other) { (void)other; }
  IGrammarSource& operator=(const IGrammarSource& other) {
    (void)other;
    return *this;
  }
};

}  // namespace Abnf

#endif
