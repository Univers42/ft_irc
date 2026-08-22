#ifndef ABNFCHARS_HPP
#define ABNFCHARS_HPP

#include <cstddef>
#include <string>

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

#endif
