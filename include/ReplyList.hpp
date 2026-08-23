#ifndef REPLYLIST_HPP
#define REPLYLIST_HPP

#define FT_IRC_REPLIES(X)        \
  X(RPL_WELCOME, "001")          \
  X(RPL_YOURHOST, "002")         \
  X(RPL_CREATED, "003")          \
  X(RPL_MYINFO, "004")           \
  X(RPL_ISUPPORT, "005")         \
                                 \
  X(RPL_UMODEIS, "221")          \
                                 \
  X(RPL_USERHOST, "302")         \
                                 \
  X(RPL_WHOISUSER, "311")        \
  X(RPL_WHOISSERVER, "312")      \
  X(RPL_ENDOFWHOIS, "318")       \
  X(RPL_WHOISCHANNELS, "319")    \
                                 \
  X(RPL_CHANNELMODEIS, "324")    \
  X(RPL_CREATIONTIME, "329")     \
                                 \
  X(RPL_NOTOPIC, "331")          \
  X(RPL_TOPIC, "332")            \
  X(RPL_TOPICWHOTIME, "333")     \
                                 \
  X(RPL_INVITING, "341")         \
                                 \
  X(RPL_WHOREPLY, "352")         \
  X(RPL_ENDOFWHO, "315")         \
                                 \
  X(RPL_NAMREPLY, "353")         \
  X(RPL_ENDOFNAMES, "366")       \
                                 \
  X(ERR_NOSUCHNICK, "401")       \
  X(ERR_NOSUCHCHANNEL, "403")    \
  X(ERR_CANNOTSENDTOCHAN, "404") \
  X(ERR_NORECIPIENT, "411")      \
  X(ERR_NOTEXTTOSEND, "412")     \
  X(ERR_UNKNOWNCOMMAND, "421")   \
  X(ERR_NOMOTD, "422")           \
  X(ERR_NONICKNAMEGIVEN, "431")  \
  X(ERR_ERRONEUSNICKNAME, "432") \
  X(ERR_NICKNAMEINUSE, "433")    \
  X(ERR_USERNOTINCHANNEL, "441") \
  X(ERR_NOTONCHANNEL, "442")     \
  X(ERR_USERONCHANNEL, "443")    \
  X(ERR_NOTREGISTERED, "451")    \
  X(ERR_NEEDMOREPARAMS, "461")   \
  X(ERR_ALREADYREGISTRED, "462") \
  X(ERR_PASSWDMISMATCH, "464")   \
  X(ERR_CHANNELISFULL, "471")    \
  X(ERR_UNKNOWNMODE, "472")      \
  X(ERR_INVITEONLYCHAN, "473")   \
  X(ERR_BANNEDFROMCHAN, "474")   \
  X(ERR_BADCHANNELKEY, "475")    \
  X(ERR_BADCHANMASK, "476")      \
  X(ERR_NOPRIVILEGES, "481")     \
  X(ERR_CHANOPRIVSNEEDED, "482") \
  X(ERR_UMODEUNKNOWNFLAG, "501") \
  X(ERR_USERSDONTMATCH, "502")   \
  X(ERR_INVALIDKEY, "525")       \
                                 \
  X(ERR_INVALIDMODEPARAM, "696")

#endif
