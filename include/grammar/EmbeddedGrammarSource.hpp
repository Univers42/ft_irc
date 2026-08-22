#ifndef EMBEDDEDGRAMMARSOURCE_HPP
#define EMBEDDEDGRAMMARSOURCE_HPP

#include <string>

#include "grammar/IGrammarSource.hpp"

/* The grammar compiled into the binary.
**
** This is the default, and it is the default for a practical reason: the
** evaluation runs `./ircserv <port> <password>` with nothing else on disk, so
** a server that refused to boot over a missing .abnf file would be a
** self-inflicted wound. The text itself lives in the .cpp -- it is this
** class's data, not a header everyone includes.
*/
class EmbeddedGrammarSource : public IGrammarSource {
 public:
  EmbeddedGrammarSource();
  virtual ~EmbeddedGrammarSource();

  virtual const char* origin() const;
  virtual bool read(std::string& out) const;
};

#endif /* EMBEDDEDGRAMMARSOURCE_HPP */
