/**
 * @file EmbeddedGrammarSource.hpp
 * @brief The RFC 2812 grammar compiled into the binary.
 *
 * This is the default source, and the reason the server needs no data files
 * to run: the entire ABNF lives as one big string literal in
 * @c src/grammar/EmbeddedGrammarSource.cpp. read() copies it out.
 *
 * @see FileGrammarSource for the override path used during development.
 */
#ifndef EMBEDDEDGRAMMARSOURCE_HPP
#define EMBEDDEDGRAMMARSOURCE_HPP

#include <string>

#include "grammar/IGrammarSource.hpp"

namespace Abnf {
/**
 * @brief IGrammarSource backed by a string literal in the .cpp.
 *
 * Stateless -- every instance yields the same text -- so it is cheap to make
 * one on the stack, which is exactly what Server::initGrammar() does.
 */
class EmbeddedGrammarSource : public IGrammarSource {
 public:
  EmbeddedGrammarSource();
  virtual ~EmbeddedGrammarSource();

  /** @brief @return The fixed string "<embedded RFC 2812 grammar>". */
  virtual const char* origin() const;

  /**
   * @brief Copies the baked-in grammar into @p out.
   * @param[out] out Receives the grammar text.
   * @return Always true -- there is nothing here that can fail.
   */
  virtual bool read(std::string& out) const;

 private:
  //< Non-copyable by convention with the rest of the module, not by necessity:
  //< there is no state to alias. Declared and never defined, so any accidental
  //< copy is a link error rather than a silent one.
  EmbeddedGrammarSource(const EmbeddedGrammarSource& other);
  EmbeddedGrammarSource& operator=(const EmbeddedGrammarSource& other);
};

}  // namespace Abnf

#endif
