#include "IrcCase.hpp"
#include "libcpp/str/case.hpp"

/* IRC's CASEMAPPING=ascii, delegated to libcpp's ASCII-only case layer.
**
** These stay as named functions rather than call sites using libcpp
** directly: the names are what tie every nick/channel comparison in the
** server to the casemapping token advertised in 005, so if that token ever
** changes there is exactly one place to change with it.
**
** The ASCII variants are the required ones -- libcpp::str::eq_nocase is
** UTF-8 aware and folds accented letters, which under ascii casemapping are
** distinct names. Collapsing them would let one user answer to another's
** nick. */

std::string ircToLower(const std::string& s) {
  return libcpp::str::ascii_to_lower(s);
}

bool ircEquals(const std::string& a, const std::string& b) {
  return libcpp::str::eq_ascii_nocase(a, b);
}
