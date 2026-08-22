#include "grammar/FileGrammarSource.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace Abnf {
FileGrammarSource::FileGrammarSource(const std::string& path) : _path(path) {}

FileGrammarSource::~FileGrammarSource() {}

const char* FileGrammarSource::origin() const { return _path.c_str(); }

bool FileGrammarSource::read(std::string& out) const {
  std::ifstream in(_path.c_str());
  if (!in.is_open()) return false;

  std::ostringstream buffer;
  buffer << in.rdbuf();
  out = buffer.str();
  return true;
}

}  // namespace Abnf
