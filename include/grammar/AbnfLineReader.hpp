/**
 * @file AbnfLineReader.hpp
 * @brief Turns raw ABNF text into one logical rule per string.
 *
 * Stage 1 of the pipeline:
 * @verbatim
 *   IGrammarSource -> AbnfLineReader -> GrammarBuilder -> Grammar -> IMatcher
 *                     ^^^^^^^^^^^^^^
 * @endverbatim
 *
 * It exists so GrammarBuilder never has to think about newlines. It does
 * exactly two jobs, both of which are about physical layout rather than
 * grammar meaning:
 *
 *  1. Strip comments -- everything from an unquoted ';' to end of line. The
 *     quote tracking matters: the ';' inside @c "a;b" is data, not a comment.
 *  2. Fold continuations -- an indented line is glued onto the one above with
 *     a single space, and RFC 5234's elided form (a line beginning @c "=/")
 *     gets the previous rule's name prepended so it reads as a whole rule.
 *
 * The output is a vector of Line, each a complete rule on one string, tagged
 * with the physical line number so later errors can point at real source.
 */
#ifndef ABNFLINEREADER_HPP
#define ABNFLINEREADER_HPP

#include <cstddef>
#include <string>
#include <vector>

namespace Abnf {
/**
 * @brief Pre-parser that unfolds ABNF source into logical lines.
 *
 * Reusable but not reentrant: read() clears its error state, so one reader
 * may serve several texts in sequence, one at a time.
 */
class AbnfLineReader {
 public:
  /** @brief One complete rule, plus where it started in the original text. */
  struct Line {
    std::string text;    //< The whole rule, continuations already joined.
    std::size_t number;  //< 1-based physical line of the rule's FIRST line.
  };

  AbnfLineReader();
  ~AbnfLineReader();

  /**
   * @brief Splits, de-comments and unfolds @p text.
   * @param text     Raw grammar source, LF or CRLF.
   * @param[out] out Receives one Line per logical rule; cleared first.
   * @return true on success; false with error() and errorLine() set.
   * @note The only failure it can report is a leading @c "=/" with no rule
   *       above it to attach to. Everything else is left for GrammarBuilder to
   *       reject -- this class validates layout, not syntax.
   */
  bool read(const std::string& text, std::vector<Line>& out);

  /** @brief @return Description of the last failure, or "" if none. */
  const std::string& error() const;

  /** @brief @return Physical line the last failure was on, or 0 if none. */
  std::size_t errorLine() const;

 private:
  //< Non-copyable: nothing here needs copying, and forbidding it keeps the
  //< error/errorLine pair from being read off a stale duplicate.
  AbnfLineReader(const AbnfLineReader& other);
  AbnfLineReader& operator=(const AbnfLineReader& other);

  /**
   * @brief Cuts @p raw at the first ';' that is not inside a quoted string.
   * @return The line with its comment removed; unchanged if it had none.
   * @note Static because it needs no reader state; it is a pure text
   *       transform over one physical line.
   */
  static std::string stripComment(const std::string& raw);

  std::string _error;      //< Last failure message; empty means success.
  std::size_t _errorLine;  //< Physical line of _error; 0 means none.
};

}  // namespace Abnf

#endif
