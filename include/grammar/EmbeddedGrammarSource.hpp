#ifndef EMBEDDEDGRAMMARSOURCE_HPP
#define EMBEDDEDGRAMMARSOURCE_HPP

#include <string>

#include "grammar/IGrammarSource.hpp"

namespace Abnf {
class EmbeddedGrammarSource : public IGrammarSource {
 public:
  EmbeddedGrammarSource();
  virtual ~EmbeddedGrammarSource();

  virtual const char* origin() const;
  virtual bool read(std::string& out) const;
};

}  // namespace Abnf

#endif
