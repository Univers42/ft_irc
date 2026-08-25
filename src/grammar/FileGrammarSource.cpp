#include "grammar/FileGrammarSource.hpp"

#include <fstream>
#include <sstream>
#include <string>

/*
** The FT_IRC_GRAMMAR override: read the grammar off disk instead of using the
** copy baked into the binary. Whole-file slurp, because AbnfLineReader needs
** the entire text before it can fold continuation lines.
**
** Note what this does NOT do: no caching, no stat, no existence check in the
** constructor. Server::initGrammar() constructs BOTH sources unconditionally
** and only reads the one it picked, so building a FileGrammarSource for a path
** that does not exist has to stay harmless.
*/
namespace Abnf {
FileGrammarSource::FileGrammarSource(const std::string& path) : _path(path) {}

FileGrammarSource::~FileGrammarSource() {}

const char* FileGrammarSource::origin() const { return _path.c_str(); }

bool FileGrammarSource::read(std::string& out) const {
  std::ifstream in(_path.c_str());
  if (!in.is_open()) return false;  //< caller turns this into a startup error naming origin()

  //< rdbuf() insertion is the C++98 way to slurp a stream whole: no line loop,
  //< no size guess, and it keeps '\r' intact for AbnfLineReader to strip.
  std::ostringstream buffer;
  buffer << in.rdbuf();
  out = buffer.str();
  return true;
}

}  // namespace Abnf
