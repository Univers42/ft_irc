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
#define RPL_BOUNCE "10"

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
#define ERR_BANNEDFROMCHAN "474"
#define ERR_BADCHANNELKEY "475"
#define ERR_BADCHANMASK "476"
#define ERR_NOPRIVILEGES "481"
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



// RFC 2812, numeric value coverage.. Those are important omissions.. 
/**


NUMERIC COVERAGE


#define RPL_ISON              "303"
#define RPL_AWAY              "301"
#define RPL_UNAWAY            "305"
#define RPL_NOWAWAY           "306"

#define RPL_WHOISOPERATOR     "313"
#define RPL_WHOISIDLE         "317"

#define RPL_WHOWASUSER        "314"
#define RPL_ENDOFWHOWAS       "369"

#define RPL_LISTSTART         "321" // obsolete in RFC 2812
#define RPL_LIST              "322"
#define RPL_LISTEND           "323"

#define RPL_UNIQOPIS          "325"

#define RPL_SUMMONING         "342"

#define RPL_INVITELIST        "346"
#define RPL_ENDOFINVITELIST   "347"
#define RPL_EXCEPTLIST        "348"
#define RPL_ENDOFEXCEPTLIST   "349"

#define RPL_VERSION           "351"

#define RPL_LINKS             "364"
#define RPL_ENDOFLINKS        "365"

#define RPL_BANLIST           "367"
#define RPL_ENDOFBANLIST      "368"

#define RPL_INFO              "371"
#define RPL_ENDOFINFO         "374"

#define RPL_MOTDSTART         "375"
#define RPL_MOTD              "372"
#define RPL_ENDOFMOTD         "376"

#define RPL_YOUREOPER         "381"
#define RPL_REHASHING         "382"
#define RPL_YOURESERVICE      "383"
#define RPL_TIME              "391"

#define RPL_USERSSTART        "392"
#define RPL_USERS              "393"
#define RPL_ENDOFUSERS        "394"
#define RPL_NOUSERS           "395"





MISSING SERVER/STATUS/STATISTICS replies
#define RPL_TRACELINK         "200"
#define RPL_TRACECONNECTING   "201"
#define RPL_TRACEHANDSHAKE   "202"
#define RPL_TRACEUNKNOWN     "203"
#define RPL_TRACEOPERATOR    "204"
#define RPL_TRACEUSER        "205"
#define RPL_TRACESERVER      "206"
#define RPL_TRACESERVICE     "207"
#define RPL_TRACENEWTYPE     "208"
#define RPL_TRACECLASS       "209"
#define RPL_TRACERECONNECT   "210"

#define RPL_STATSLINKINFO    "211"
#define RPL_STATSCOMMANDS    "212"
#define RPL_ENDOFSTATS       "219"

#define RPL_STATSUPTIME      "242"
#define RPL_STATSOLINE       "243"

#define RPL_SERVLIST         "234"
#define RPL_SERVLISTEND      "235"

#define RPL_LUSERCLIENT      "251"
#define RPL_LUSEROP          "252"
#define RPL_LUSERUNKNOWN     "253"
#define RPL_LUSERCHANNELS    "254"
#define RPL_LUSERME          "255"

#define RPL_ADMINME          "256"
#define RPL_ADMINLOC1        "257"
#define RPL_ADMINLOC2        "258"
#define RPL_ADMINEMAIL       "259"

#define RPL_TRYAGAIN         "263"

#define RPL_TRACELOG         "261"
#define RPL_TRACEEND         "262"






MISSING ERROR NUMERICS
#define ERR_NOSUCHSERVER        "402"
#define ERR_TOOMANYCHANNELS     "405"
#define ERR_WASNOSUCHNICK       "406"
#define ERR_TOOMANYTARGETS      "407"
#define ERR_NOSUCHSERVICE       "408"
#define ERR_NOORIGIN            "409"

#define ERR_NOTOPLEVEL          "413"
#define ERR_WILDTOPLEVEL        "414"
#define ERR_BADMASK             "415"

#define ERR_NOADMININFO         "423"
#define ERR_FILEERROR           "424"

#define ERR_NICKCOLLISION       "436"
#define ERR_UNAVAILRESOURCE     "437"

#define ERR_NOLOGIN             "444"
#define ERR_SUMMONDISABLED      "445"
#define ERR_USERSDISABLED       "446"

#define ERR_NOPERMFORHOST       "463"
#define ERR_YOUREBANNEDCREEP    "465"
#define ERR_YOUWILLBEBANNED     "466"
#define ERR_KEYSET              "467"

#define ERR_BANNEDFROMCHAN      "474"
#define ERR_NOCHANMODES         "477"
#define ERR_BANLISTFULL         "478"

#define ERR_NOPRIVILEGES        "481"
#define ERR_CANTKILLSERVER      "483"
#define ERR_RESTRICTED          "484"
#define ERR_UNIQOPPRIVSNEEDED   "485"

#define ERR_NOOPERHOST          "491"
#define ERR_UMODEUNKNOWNFLAG    "501"

*/



/**

    RPL_WELCOME (001)
    RPL_YOURHOST (002)
    RPL_CREATED (003)
    RPL_MYINFO (004)
    RPL_ISUPPORT (005)
    RPL_BOUNCE (010)
    RPL_STATSCOMMANDS (212)
    RPL_ENDOFSTATS (219)
    RPL_UMODEIS (221)
    RPL_STATSUPTIME (242)
    RPL_LUSERCLIENT (251)
    RPL_LUSEROP (252)
    RPL_LUSERUNKNOWN (253)
    RPL_LUSERCHANNELS (254)
    RPL_LUSERME (255)
    RPL_ADMINME (256)
    RPL_ADMINLOC1 (257)
    RPL_ADMINLOC2 (258)
    RPL_ADMINEMAIL (259)
    RPL_TRYAGAIN (263)
    RPL_LOCALUSERS (265)
    RPL_GLOBALUSERS (266)
    RPL_WHOISCERTFP (276)
    RPL_NONE (300)
    RPL_AWAY (301)
    RPL_USERHOST (302)
    RPL_UNAWAY (305)
    RPL_NOWAWAY (306)
    RPL_WHOISREGNICK (307)
    RPL_WHOISUSER (311)
    RPL_WHOISSERVER (312)
    RPL_WHOISOPERATOR (313)
    RPL_WHOWASUSER (314)
    RPL_ENDOFWHO (315)
    RPL_WHOISIDLE (317)
    RPL_ENDOFWHOIS (318)
    RPL_WHOISCHANNELS (319)
    RPL_WHOISSPECIAL (320)
    RPL_LISTSTART (321)
    RPL_LIST (322)
    RPL_LISTEND (323)
    RPL_CHANNELMODEIS (324)
    RPL_CREATIONTIME (329)
    RPL_WHOISACCOUNT (330)
    RPL_NOTOPIC (331)
    RPL_TOPIC (332)
    RPL_TOPICWHOTIME (333)
    RPL_INVITELIST (336)
    RPL_ENDOFINVITELIST (337)
    RPL_WHOISACTUALLY (338)
    RPL_INVITING (341)
    RPL_INVEXLIST (346)
    RPL_ENDOFINVEXLIST (347)
    RPL_EXCEPTLIST (348)
    RPL_ENDOFEXCEPTLIST (349)
    RPL_VERSION (351)
    RPL_WHOREPLY (352)
    RPL_NAMREPLY (353)
    RPL_LINKS (364)
    RPL_ENDOFLINKS (365)
    RPL_ENDOFNAMES (366)
    RPL_BANLIST (367)
    RPL_ENDOFBANLIST (368)
    RPL_ENDOFWHOWAS (369)
    RPL_INFO (371)
    RPL_MOTD (372)
    RPL_ENDOFINFO (374)
    RPL_MOTDSTART (375)
    RPL_ENDOFMOTD (376)
    RPL_WHOISHOST (378)
    RPL_WHOISMODES (379)
    RPL_YOUREOPER (381)
    RPL_REHASHING (382)
    RPL_TIME (391)
    ERR_UNKNOWNERROR (400)
    ERR_NOSUCHNICK (401)
    ERR_NOSUCHSERVER (402)
    ERR_NOSUCHCHANNEL (403)
    ERR_CANNOTSENDTOCHAN (404)
    ERR_TOOMANYCHANNELS (405)
    ERR_WASNOSUCHNICK (406)
    ERR_NOORIGIN (409)
    ERR_NORECIPIENT (411)
    ERR_NOTEXTTOSEND (412)
    ERR_INPUTTOOLONG (417)
    ERR_UNKNOWNCOMMAND (421)
    ERR_NOMOTD (422)
    ERR_NONICKNAMEGIVEN (431)
    ERR_ERRONEUSNICKNAME (432)
    ERR_NICKNAMEINUSE (433)
    ERR_NICKCOLLISION (436)
    ERR_USERNOTINCHANNEL (441)
    ERR_NOTONCHANNEL (442)
    ERR_USERONCHANNEL (443)
    ERR_NOTREGISTERED (451)
    ERR_NEEDMOREPARAMS (461)
    ERR_ALREADYREGISTERED (462)
    ERR_PASSWDMISMATCH (464)
    ERR_YOUREBANNEDCREEP (465)
    ERR_CHANNELISFULL (471)
    ERR_UNKNOWNMODE (472)
    ERR_INVITEONLYCHAN (473)
    ERR_BANNEDFROMCHAN (474)
    ERR_BADCHANNELKEY (475)
    ERR_BADCHANMASK (476)
    ERR_NOPRIVILEGES (481)
    ERR_CHANOPRIVSNEEDED (482)
    ERR_CANTKILLSERVER (483)
    ERR_NOOPERHOST (491)
    ERR_UMODEUNKNOWNFLAG (501)
    ERR_USERSDONTMATCH (502)
    ERR_HELPNOTFOUND (524)
    ERR_INVALIDKEY (525)
    RPL_STARTTLS (670)
    RPL_WHOISSECURE (671)
    ERR_STARTTLS (691)
    ERR_INVALIDMODEPARAM (696)
    RPL_HELPSTART (704)
    RPL_HELPTXT (705)
    RPL_ENDOFHELP (706)
    ERR_NOPRIVS (723)
    RPL_LOGGEDIN (900)
    RPL_LOGGEDOUT (901)
    ERR_NICKLOCKED (902)
    RPL_SASLSUCCESS (903)
    ERR_SASLFAIL (904)
    ERR_SASLTOOLONG (905)
    ERR_SASLABORTED (906)
    ERR_SASLALREADY (907)
    RPL_SASLMECHS (908)

RPL_ISUPPORT Parameters

    AWAYLEN Parameter
    CASEMAPPING Parameter
    CHANLIMIT Parameter
    CHANMODES Parameter
    CHANNELLEN Parameter
    CHANTYPES Parameter
    ELIST Parameter
    EXCEPTS Parameter
    EXTBAN Parameter
    HOSTLEN Parameter
    INVEX Parameter
    KICKLEN Parameter
    MAXLIST Parameter
    MAXTARGETS Parameter
    MODES Parameter
    NETWORK Parameter
    NICKLEN Parameter
    PREFIX Parameter
    SAFELIST Parameter
    SILENCE Parameter
    STATUSMSG Parameter
    TARGMAX Parameter
    TOPICLEN Parameter
    USERLEN Parameter

*/
#endif
