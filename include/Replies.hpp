#ifndef REPLIES_HPP
#define REPLIES_HPP

#include <sstream>
#include <string>

/* ─── Reply numerics (RFC 2812 + de-facto standards) ─── */

// Registration
#define RPL_WELCOME "001"
#define RPL_YOURHOST "002"
#define RPL_CREATED "003"
#define RPL_MYINFO "004"
#define RPL_ISUPPORT "005"

// User mode
#define RPL_UMODEIS "221"

// USERHOST
#define RPL_USERHOST "302"

// WHOIS
#define RPL_WHOISUSER "311"
#define RPL_WHOISSERVER "312"
#define RPL_ENDOFWHOIS "318"
#define RPL_WHOISCHANNELS "319"

// Channel mode
#define RPL_CHANNELMODEIS "324"
#define RPL_CREATIONTIME "329"

// Topic
#define RPL_NOTOPIC "331"
#define RPL_TOPIC "332"
#define RPL_TOPICWHOTIME "333"

// Invite
#define RPL_INVITING "341"

// WHO
#define RPL_WHOREPLY "352"
#define RPL_ENDOFWHO "315"

// NAMES
#define RPL_NAMREPLY "353"
#define RPL_ENDOFNAMES "366"

/* ─── Error numerics ─── */

#define ERR_NOSUCHNICK "401"
#define ERR_NOSUCHCHANNEL "403"
#define ERR_CANNOTSENDTOCHAN "404"
#define ERR_NORECIPIENT "411"
#define ERR_NOTEXTTOSEND "412"
#define ERR_UNKNOWNCOMMAND "421"
#define ERR_NOMOTD "422"
#define ERR_NONICKNAMEGIVEN "431"
#define ERR_ERRONEUSNICKNAME "432"
#define ERR_NICKNAMEINUSE "433"
#define ERR_USERNOTINCHANNEL "441"
#define ERR_NOTONCHANNEL "442"
#define ERR_USERONCHANNEL "443"
#define ERR_NOTREGISTERED "451"
#define ERR_NEEDMOREPARAMS "461"
#define ERR_ALREADYREGISTRED "462"
#define ERR_PASSWDMISMATCH "464"
#define ERR_CHANNELISFULL "471"
#define ERR_UNKNOWNMODE "472"
#define ERR_INVITEONLYCHAN "473"
#define ERR_BADCHANNELKEY "475"
#define ERR_BADCHANMASK "476"
#define ERR_CHANOPRIVSNEEDED "482"
#define ERR_INVALIDKEY "525"
#define ERR_USERSDONTMATCH "502"
/* Generic "that mode parameter is unusable" (modern ircd numeric). Used for
** +l, which has no dedicated numeric the way +k has ERR_INVALIDKEY.
** Format: <client> <target> <mode char> <parameter> :<reason> */
#define ERR_INVALIDMODEPARAM "696"

/* ─── Server configuration constants ─── */

#define SERVER_NAME "ft_irc"
#define SERVER_VERSION "1.0"
#define SERVER_CREATED "2025-01-01"

#define MAX_NICKLEN 9
/* USER's username lands in every relayed line's prefix (nick!user@host), so
** an unbounded one eats into the 512-byte payload budget -- silently, since
** Client::queueMessage truncates. 10 is the classic ircd USERLEN. */
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
/* Safety net for deferred disconnects: seconds a client marked
** pending-close gets to drain its remaining _out before we give up on
** the peer and close anyway. Deliberately much smaller than the ping
** keepalive above -- this bounds teardown of a connection the server
** already decided to end, not liveness of one still in use. */
#define PENDING_CLOSE_TIMEOUT 5

#endif
