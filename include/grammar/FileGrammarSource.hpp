/**
 * @file FileGrammarSource.hpp
 * @brief IGrammarSource that slurps an .abnf file from disk.
 *
 * Selected when the FT_IRC_GRAMMAR environment variable names a path. It lets
 * the grammar be edited and reloaded without a rebuild, which is how the
 * grammar in EmbeddedGrammarSource.cpp was developed in the first place.
 */
#ifndef FILEGRAMMARSOURCE_HPP
#define FILEGRAMMARSOURCE_HPP

#include <string>

#include "grammar/IGrammarSource.hpp"

namespace Abnf {
/**
 * @brief IGrammarSource reading one file, opened fresh on every read().
 *
 * The path is captured at construction; the file is not touched until read()
 * is called, so constructing one for a path that does not exist is harmless.
 * Server::initGrammar() relies on that: it builds both sources unconditionally
 * and only reads the one it picked.
 */
class FileGrammarSource : public IGrammarSource {
 public:
  /**
   * @brief Remembers a path; does not open anything yet.
   * @param path Filesystem path to the .abnf file.
   * @note @c explicit so a bare std::string never converts into a source by
   *       accident at a call site expecting the interface.
   */
  explicit FileGrammarSource(const std::string& path);
  virtual ~FileGrammarSource();

  /** @brief @return The path given to the constructor, as a C string. */
  virtual const char* origin() const;

  /**
   * @brief Reads the whole file into @p out.
   * @param[out] out Receives the file contents verbatim, CRLF and all --
   *             AbnfLineReader is the one that strips '\r'.
   * @return true if the file opened; false if it could not be, which
   *         Server::initGrammar() turns into a startup error naming origin().
   */
  virtual bool read(std::string& out) const;

 private:
  //< No default ctor: a source without a path could only ever fail.
  FileGrammarSource();
  //< Non-copyable, matching the rest of the module.
  FileGrammarSource(const FileGrammarSource& other);
  FileGrammarSource& operator=(const FileGrammarSource& other);

  std::string _path;  //< Owned copy; origin() hands out a pointer into it.
};

}  // namespace Abnf

#endif
