#ifndef ABNFCHARS_HPP
#define ABNFCHARS_HPP

#include <cstddef>
#include <string>

/* Character classification for ABNF text.
**
** Spelled out rather than taken from <cctype>. isalpha/isalnum are
** locale-sensitive, and under a non-C locale they would quietly admit 8-bit
** octets into rule names. The rest of this project avoids them for the same
** reason -- see vendor/libcpp/src/str/case.cpp.
*/
namespace AbnfChars {

bool isAlpha(char c);
bool isDigit(char c);
bool isHexDigit(char c);
bool isRuleChar(char c);
bool isBlank(char c);

char toLower(char c);
std::string lowered(const std::string& s);

void skipBlanks(const std::string& s, std::size_t& i);
std::string trimmed(const std::string& s);

}  // namespace AbnfChars

#endif /* ABNFCHARS_HPP */
