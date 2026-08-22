#ifndef ABNFLINEREADER_HPP
#define ABNFLINEREADER_HPP

#include <cstddef>
#include <string>
#include <vector>

namespace Abnf {
class AbnfLineReader {
 public:
  struct Line {
    std::string text;
    std::size_t number;
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

}  // namespace Abnf

#endif
