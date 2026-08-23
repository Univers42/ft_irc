#ifndef FILEGRAMMARSOURCE_HPP
#define FILEGRAMMARSOURCE_HPP

#include <string>

#include "grammar/IGrammarSource.hpp"

namespace Abnf {
class FileGrammarSource : public IGrammarSource {
 public:
  explicit FileGrammarSource(const std::string& path);
  virtual ~FileGrammarSource();

  virtual const char* origin() const;
  virtual bool read(std::string& out) const;

 private:
  FileGrammarSource();
  FileGrammarSource(const FileGrammarSource& other);
  FileGrammarSource& operator=(const FileGrammarSource& other);

  std::string _path;
};

}  // namespace Abnf

#endif
