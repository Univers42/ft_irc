#ifndef EMBEDDEDGRAMMARSOURCE_HPP
#define EMBEDDEDGRAMMARSOURCE_HPP

#include <string>

#include "grammar/IGrammarSource.hpp"

class EmbeddedGrammarSource : public IGrammarSource {
 public:
  EmbeddedGrammarSource();
  virtual ~EmbeddedGrammarSource();

  virtual const char* origin() const;
  virtual bool read(std::string& out) const;
};

#endif
