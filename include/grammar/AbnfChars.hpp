/**
 * @file AbnfChars.hpp
 * @brief ASCII character predicates for the ABNF parser.
 *
 * A deliberately small replacement for <cctype>. Two reasons it exists:
 *
 *  1. @c isalpha / @c isdigit are locale-dependent and take an @c int. Passing
 *     a plain @c char that happens to be negative -- any byte >= 0x80, which
 *     the @c nospcrlfcl rule is full of -- is undefined behaviour. These take
 *     @c char and are ASCII-only by definition, which is what RFC 5234 means.
 *  2. isRuleChar() and isBlank() are not character classes at all; they are
 *     ABNF grammar productions (@c rulename body, @c c-wsp) and have no
 *     standard-library equivalent.
 *
 * Used by AbnfLineReader (folding, comments) and GrammarBuilder (tokenising).
 */
#ifndef ABNFCHARS_HPP
#define ABNFCHARS_HPP

#include <cstddef>
#include <string>

namespace Abnf {
/** @brief Free functions; no state, no class -- a namespace is enough. */
namespace AbnfChars {

/** @brief ABNF @c ALPHA = %x41-5A / %x61-7A. @return true for 'A'..'Z', 'a'..'z'. */
bool isAlpha(char c);

/** @brief ABNF @c DIGIT = %x30-39; the "14" in @c *14(SPACE middle). */
bool isDigit(char c);

/** @brief ABNF @c HEXDIG, either case; the digits of a @c %x41-5A range. */
bool isHexDigit(char c);

/**
 * @brief A character legal inside a rule name: letter, digit or '-'.
 * @note This is what makes "user-cmd" and "3digit" scan as one token, and
 *       what makes '_', '$' and '(' terminate a name.
 */
bool isRuleChar(char c);

/**
 * @brief Horizontal whitespace: space or tab.
 * @note This is ABNF @c c-wsp, NOT the grammar's own @c SPACE rule. A leading
 *       blank is how AbnfLineReader recognises a continuation line.
 */
bool isBlank(char c);

/** @brief ASCII-only lower-casing of one character; leaves everything else alone. */
char toLower(char c);

/**
 * @brief ASCII-only lower-casing of a whole string.
 * @note Rule names are folded through this, so "SPACE" and "space" name the
 *       same rule, as RFC 5234 requires. Capture names are NOT folded.
 */
std::string lowered(const std::string& s);

/**
 * @brief Advances @p i past any run of blanks.
 * @param s Text being scanned.
 * @param[in,out] i Cursor; on return points at the first non-blank, or at
 *                s.size(). Never moves backwards.
 */
void skipBlanks(const std::string& s, std::size_t& i);

/**
 * @brief @return @p s with leading and trailing blanks removed.
 * @note An all-blank string trims to "", which is how AbnfLineReader detects
 *       a line that held nothing but a comment.
 */
std::string trimmed(const std::string& s);

}  // namespace AbnfChars

}  // namespace Abnf

#endif
