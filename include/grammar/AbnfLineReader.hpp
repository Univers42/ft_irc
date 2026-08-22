#ifndef ABNFLINEREADER_HPP
#define ABNFLINEREADER_HPP

#include <cstddef>
#include <string>
#include <vector>

/* Turns raw ABNF text into one logical line per rule.
**
** Three things happen here, all of them lexical, none of them parsing:
** comments are stripped, indented continuations are folded into the line
** above, and the RFC's name-elided incremental alternative is expanded.
**
** That last one is the reason this is its own class. RFC 2812 writes:
**
**     params     =  *14( SPACE middle ) [ SPACE ":" trailing ]
**                =/ 14( SPACE middle ) [ SPACE [ ":" ] trailing ]
**
** The second line is indented, so a naive folder glues it onto the first and
** produces nonsense. A continuation whose first token is '=' is instead
** treated as a fresh definition of the most recent rule name -- which is
** exactly what the RFC means by it.
*/
class AbnfLineReader {
 public:
  struct Line {
    std::string text;
    std::size_t number; /* the physical line it started on, for error reports */
  };

  AbnfLineReader();

  bool read(const std::string& text, std::vector<Line>& out);

  const std::string& error() const;
  std::size_t errorLine() const;

 private:
  static std::string stripComment(const std::string& raw);

  std::string _error;
  std::size_t _errorLine;
};

#endif /* ABNFLINEREADER_HPP */
