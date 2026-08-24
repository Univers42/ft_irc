#ifndef IRCNAME_HPP
#define IRCNAME_HPP

#include <string>

#include "Limits.hpp"

/*
** Is this a well-formed nickname / channel name / channel key / username?
**
** These lived on Server as const member functions, but none of them ever read
** a member: they are pure functions of the string and the Limits bounds. That
** cost them their testability -- reaching them meant standing up a Server --
** and it is why the character rules below had no direct unit test at all,
** only remarks about them in test_conformance.cpp.
**
** Header-only, like ChannelModes and Limits, so the bounds and the character
** rules they belong to stay in one file.
*/
namespace IrcName {

inline bool isAsciiAlpha(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

inline bool isAsciiDigit(char c) { return c >= '0' && c <= '9'; }

inline bool isNickSpecial(char c) {
  const unsigned char u = static_cast<unsigned char>(c);
  return (u >= 0x5B && u <= 0x60) ||
         (u >= 0x7B && u <= 0x7D);  //< RFC special · [ \\ ] ^ _ ` and { | } · NINE, ` was the D1 bug
}

//< nick first octet · "alice" "[bot]" "z`tick" ok · "1abc" "-x" "#c" no (no digit, no '-')
inline bool isNickLead(char c) { return isAsciiAlpha(c) || isNickSpecial(c); }

//< nick tail · digits and '-' now allowed · "a1" "z-9" "n|ck" ok · "a.b" "a,b" no
inline bool isNickBody(char c) { return isAsciiAlpha(c) || isAsciiDigit(c) || isNickSpecial(c) || c == '-'; }

inline bool isUsernameChar(unsigned char c) {
  return (c >= 0x01 && c <= 0x09) || (c >= 0x0B && c <= 0x0C) || (c >= 0x0E && c <= 0x1F) || (c >= 0x21 && c <= 0x3F) ||
         (c >= 0x41);
}

//< the 9-octet bound is the grammar's own nickname production, not a separate
//< policy: ( letter / special ) *8( letter / digit / special / "-" )
inline bool isNickname(const std::string& nick) {
  if (nick.empty()) return false;
  if (nick.size() > Limits::kNickLen) return false;

  if (!isNickLead(nick[0])) return false;
  for (std::string::size_type i = 1; i < nick.size(); ++i)
    if (!isNickBody(nick[i])) return false;
  return true;
}

inline bool isChannelName(const std::string& name) {
  if (name.empty() || name.size() > Limits::kChannelLen) return false;
  if (name[0] != '#') return false;   //< only '#' · RFC also allows & + ! but 005 CHANTYPES advertises just '#'
  if (name.size() < 2) return false;  //< bare "#" has no chanstring after the prefix

  for (std::string::size_type i = 0; i < name.size(); ++i) {
    if (name[i] == ' ' || name[i] == '\x07' || name[i] == ',')
      return false;                    //< SPACE ends a param · ',' splits JOIN lists
    if (name[i] == ':') return false;  //< "#a:b" -> 476 · D4: stricter than RFC, which reads it as name:mask
  }
  return true;
}

inline bool isChannelKey(const std::string& key) {
  if (key.empty() || key.size() > Limits::kKeyLen) return false;  //< "" · 24 octets · RFC key = 1*23(...)
  for (std::string::size_type i = 0; i < key.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(key[i]);
    if (c <= ' ' || c == ',') return false;  //< SPACE ends the param · ',' splits JOIN's key list
    if (c > 0x7F) return false;              //< key is 7-bit · "sécret" -> 525 · D5 · shared with JOIN so both agree
  }
  return true;
}

inline bool isUsername(const std::string& user) {
  if (user.empty()) return false;
  for (std::string::size_type i = 0; i < user.size(); ++i)
    if (!isUsernameChar(static_cast<unsigned char>(user[i]))) return false;
  return true;
}

}  // namespace IrcName

#endif
