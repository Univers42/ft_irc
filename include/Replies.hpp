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

#define SERVER_NAME "ft_irc"
#define SERVER_VERSION "1.0"
#define SERVER_CREATED "2025-01-01"

#define MAX_NICKLEN 9

#define MAX_USERLEN 10
#define MAX_CHANNELLEN 50
#define MAX_TOPICLEN 390
#define MAX_MSGLEN 512
#define MAX_KEYLEN 23
#define MAX_USERLIMIT 65535
#define MAX_SENDQ (64 * 1024)
#define MAX_CLIENTS 1024
#define PING_INTERVAL 120
#define PING_TIMEOUT 120

#define PENDING_CLOSE_TIMEOUT 5

#endif
