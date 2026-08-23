#include "grammar/AbnfChars.hpp"

#include <string>

namespace Abnf {
namespace AbnfChars {
//< ABNF ALPHA = %x41-5A / %x61-7A · 'A' 'q' 'Z' ok · '_' '5' '-' no
bool isAlpha(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

//< ABNF DIGIT = %x30-39 · the "14" in *14( SPACE middle ) · 'a' no
bool isDigit(char c) { return c >= '0' && c <= '9'; }

//< HEXDIG, both cases · %x41-5A in "letter = %x41-5A" · 'g' 'x' no
bool isHexDigit(char c) { return isDigit(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'); }

//< rulename body · "user-cmd" "3digit" whole · '_' '$' '(' stop it
bool isRuleChar(char c) { return isAlpha(c) || isDigit(c) || c == '-'; }

//< c-wsp, NOT the SPACE production · leading blank marks a continuation line
bool isBlank(char c) { return c == ' ' || c == '\t'; }

//< rulename fold · "SPACE"->"space", so "space" and "SPACE" are one rule (RFC 5234)
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
  while (i < s.size() && isBlank(s[i])) ++i;  //< "  = *14(x)" -> i lands on '='
}

std::string trimmed(const std::string& s) {
  std::string::size_type b = 0;
  std::string::size_type e = s.size();
  while (b < e && isBlank(s[b])) ++b;      //< left edge · "   params = x" -> b at 'p'
  while (e > b && isBlank(s[e - 1])) --e;  //< right edge · all-blank -> b==e -> ""
  return s.substr(b, e - b);
}

}  // namespace AbnfChars

}  // namespace Abnf
