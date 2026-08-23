#ifndef REPLIES_HPP
#define REPLIES_HPP

#include <sstream>
#include <string>

#include "ReplyList.hpp"

#define FT_IRC_REPLY_CODE(replyName, replyCode, replyText) const char* const replyName = replyCode;
FT_IRC_REPLIES(FT_IRC_REPLY_CODE)
#undef FT_IRC_REPLY_CODE

namespace ReplyText {
struct Entry {
  const char* code;
  const char* text;
};

inline const Entry* table() {
#define FT_IRC_REPLY_TEXT(replyName, replyCode, replyText) {replyCode, replyText},
  static const Entry kTexts[] = {FT_IRC_REPLIES(FT_IRC_REPLY_TEXT){0, 0}};
#undef FT_IRC_REPLY_TEXT
  return kTexts;
}

inline const char* find(const std::string& code) {
  for (const Entry* entry = table(); entry->code != 0; ++entry)
    if (code == entry->code) return entry->text;
  return 0;
}
}  // namespace ReplyText

#endif
