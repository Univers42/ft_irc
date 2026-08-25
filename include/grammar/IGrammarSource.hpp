/**
 * @file IGrammarSource.hpp
 * @brief Where the ABNF text comes from, decoupled from who parses it.
 *
 * Stage 0 of the grammar pipeline:
 * @verbatim
 *   IGrammarSource -> AbnfLineReader -> GrammarBuilder -> Grammar -> IMatcher -> MatchResult
 *   ^^^^^^^^^^^^^^
 * @endverbatim
 *
 * Two implementations exist and they are interchangeable at the call site:
 *   - EmbeddedGrammarSource -- the RFC 2812 grammar baked into the binary.
 *   - FileGrammarSource     -- an .abnf file on disk, for experiments.
 *
 * Server::initGrammar() picks between them from the FT_IRC_GRAMMAR environment
 * variable and then treats the result as an IGrammarSource&, so nothing
 * downstream knows or cares which one it got.
 */
#ifndef IGRAMMARSOURCE_HPP
#define IGRAMMARSOURCE_HPP

#include <string>

namespace Abnf {
/**
 * @brief Abstract provider of one blob of ABNF source text.
 *
 * Deliberately tiny: a name for error messages and a way to get the bytes.
 * Reading is @c const because a source is a value to be read, never a
 * stateful stream -- read() may be called twice and must yield the same text.
 */
class IGrammarSource {
 public:
  virtual ~IGrammarSource() {}

  /**
   * @brief Human-readable name of this source, for error messages.
   * @return A NUL-terminated string such as a file path, or
   *         "<embedded RFC 2812 grammar>". Never NULL.
   * @note Returned as @c const @c char* rather than a string so an
   *       implementation may hand back a literal with no allocation.
   */
  virtual const char* origin() const = 0;

  /**
   * @brief Loads the whole grammar text.
   * @param[out] out Receives the complete source; overwritten, not appended.
   * @return true on success; false if the source could not be read (a missing
   *         file, say). @p out is unspecified on failure.
   * @note Whole-blob, not line-by-line: AbnfLineReader wants the entire text
   *       because a rule may fold across several physical lines.
   */
  virtual bool read(std::string& out) const = 0;

 protected:
  /** @brief Protected so only derived classes can construct one. */
  IGrammarSource() {}
  /** @brief Protected copy ctor; a base subobject carries no state to copy. */
  IGrammarSource(const IGrammarSource& other) { (void)other; }
  /** @brief Protected assignment, kept for symmetry with the copy ctor. */
  IGrammarSource& operator=(const IGrammarSource& other) {
    (void)other;
    return *this;
  }
};

}  // namespace Abnf

#endif
