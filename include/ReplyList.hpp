#ifndef REPLYLIST_HPP
#define REPLYLIST_HPP

#define FT_IRC_REPLIES(X)                                           \
  X(RPL_WELCOME, "001", 0)                                          \
  X(RPL_YOURHOST, "002", 0)                                         \
  X(RPL_CREATED, "003", 0)                                          \
  X(RPL_MYINFO, "004", 0)                                           \
  X(RPL_ISUPPORT, "005", 0)                                         \
                                                                    \
  X(RPL_UMODEIS, "221", 0)                                          \
                                                                    \
  X(RPL_USERHOST, "302", 0)                                         \
                                                                    \
  X(RPL_WHOISUSER, "311", 0)                                        \
  X(RPL_WHOISSERVER, "312", "ft_irc server")                        \
  X(RPL_ENDOFWHOIS, "318", "End of WHOIS list")                     \
  X(RPL_WHOISCHANNELS, "319", 0)                                    \
                                                                    \
  X(RPL_CHANNELMODEIS, "324", 0)                                    \
  X(RPL_CREATIONTIME, "329", 0)                                     \
                                                                    \
  X(RPL_NOTOPIC, "331", "No topic is set")                          \
  X(RPL_TOPIC, "332", 0)                                            \
  X(RPL_TOPICWHOTIME, "333", 0)                                     \
                                                                    \
  X(RPL_INVITING, "341", 0)                                         \
                                                                    \
  X(RPL_WHOREPLY, "352", 0)                                         \
  X(RPL_ENDOFWHO, "315", "End of WHO list")                         \
                                                                    \
  X(RPL_NAMREPLY, "353", 0)                                         \
  X(RPL_ENDOFNAMES, "366", "End of /NAMES list")                    \
                                                                    \
  X(ERR_NOSUCHNICK, "401", "No such nick/channel")                  \
  X(ERR_NOSUCHCHANNEL, "403", "No such channel")                    \
  X(ERR_CANNOTSENDTOCHAN, "404", "Cannot send to channel")          \
  X(ERR_NORECIPIENT, "411", "No recipient given (PRIVMSG)")         \
  X(ERR_NOTEXTTOSEND, "412", "No text to send")                     \
  X(ERR_UNKNOWNCOMMAND, "421", "Unknown command")                   \
  X(ERR_NOMOTD, "422", "MOTD File is missing")                      \
  X(ERR_NONICKNAMEGIVEN, "431", "No nickname given")                \
  X(ERR_ERRONEUSNICKNAME, "432", "Erroneous nickname")              \
  X(ERR_NICKNAMEINUSE, "433", "Nickname is already in use")         \
  X(ERR_USERNOTINCHANNEL, "441", "They aren't on that channel")     \
  X(ERR_NOTONCHANNEL, "442", "You're not on that channel")          \
  X(ERR_USERONCHANNEL, "443", "is already on channel")              \
  X(ERR_NOTREGISTERED, "451", "You have not registered")            \
  X(ERR_NEEDMOREPARAMS, "461", "Not enough parameters")             \
  X(ERR_ALREADYREGISTRED, "462", "You may not reregister")          \
  X(ERR_PASSWDMISMATCH, "464", "Password incorrect")                \
  X(ERR_CHANNELISFULL, "471", "Cannot join channel (+l)")           \
  X(ERR_UNKNOWNMODE, "472", "is unknown mode char to me")           \
  X(ERR_INVITEONLYCHAN, "473", "Cannot join channel (+i)")          \
  X(ERR_BANNEDFROMCHAN, "474", 0)                                   \
  X(ERR_BADCHANNELKEY, "475", "Cannot join channel (+k)")           \
  X(ERR_BADCHANMASK, "476", "Bad Channel Mask")                     \
  X(ERR_NOPRIVILEGES, "481", 0)                                     \
  X(ERR_CHANOPRIVSNEEDED, "482", "You're not channel operator")     \
  X(ERR_UMODEUNKNOWNFLAG, "501", "is unknown mode char to me")      \
  X(ERR_USERSDONTMATCH, "502", "Can't change mode for other users") \
  X(ERR_INVALIDKEY, "525", "Key is not well-formed")                \
                                                                    \
  X(ERR_INVALIDMODEPARAM, "696", "Invalid channel limit")

#endif
