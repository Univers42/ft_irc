#ifndef IGRAMMARSOURCE_HPP
#define IGRAMMARSOURCE_HPP

#include <string>

/* Where the grammar text comes from.
**
** The same seam idea as IServerExtension: the server asks for grammar text and
** never names a concrete source, so swapping the embedded default for a file
** on disk is a construction-site decision rather than an #ifdef.
**
** read() returns false when the text could not be obtained; the caller reports
** that and refuses to start, because a server with no grammar can answer
** nothing.
*/
class IGrammarSource {
 public:
  virtual ~IGrammarSource() {}

  /* Human-readable origin, used in startup diagnostics: a reader who sees a
  ** grammar error needs to know which grammar failed. */
  virtual const char* origin() const = 0;

  virtual bool read(std::string& out) const = 0;
};

#endif /* IGRAMMARSOURCE_HPP */
