#ifndef FILEGRAMMARSOURCE_HPP
#define FILEGRAMMARSOURCE_HPP

#include <string>

#include "grammar/IGrammarSource.hpp"

/* A grammar read from a file, for experimenting with the wire syntax without
** rebuilding. Selected by $FT_IRC_GRAMMAR; the subject's file list allows an
** optional configuration file, and this is it.
*/
class FileGrammarSource : public IGrammarSource {
 public:
  explicit FileGrammarSource(const std::string& path);
  virtual ~FileGrammarSource();

  virtual const char* origin() const;
  virtual bool read(std::string& out) const;

 private:
  std::string _path;
};

#endif /* FILEGRAMMARSOURCE_HPP */
