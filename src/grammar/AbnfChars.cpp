#include "grammar/AbnfChars.hpp"

#include <string>

namespace Abnf {
namespace AbnfChars {
bool isAlpha(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

bool isDigit(char c) { return c >= '0' && c <= '9'; }

bool isHexDigit(char c) { return isDigit(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'); }

bool isRuleChar(char c) { return isAlpha(c) || isDigit(c) || c == '-'; }

bool isBlank(char c) { return c == ' ' || c == '\t'; }

char toLower(char c) {
  if (c >= 'A' && c <= 'Z') return static_cast<char>(c - 'A' + 'a');
  return c;
}

std::string lowered(const std::string& s) {
  std::string out(s);
  for (std::string::size_type i = 0; i < out.size(); ++i) out[i] = toLower(out[i]);
  return out;
}

void skipBlanks(const std::string& s, std::size_t& i) {
  while (i < s.size() && isBlank(s[i])) ++i;
}

std::string trimmed(const std::string& s) {
  std::string::size_type b = 0;
  std::string::size_type e = s.size();
  while (b < e && isBlank(s[b])) ++b;
  while (e > b && isBlank(s[e - 1])) --e;
  return s.substr(b, e - b);
}

}  // namespace AbnfChars

}  // namespace Abnf
