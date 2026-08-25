#include <string>

#include "IrcCase.hpp"
#include "IrcMessage.hpp"
#include "IrcName.hpp"
#include "IrcTrace.hpp"
#include "Limits.hpp"
#include "Log.hpp"
#include "Server.hpp"
#include "Settings.hpp"
#include "ext/IServerExtension.hpp"
#include "libcpp/str/case.hpp"
#include "libcpp/str/format.hpp"
#include "libcpp/str/secure.hpp"

void Server::cmdCap(Client* client, const Message& msg) {
  if (msg.params.empty()) return;

  std::string subcommand = libcpp::str::to_upper(msg.params[0]);

  if (subcommand == "LS") {
    client->queueMessage(":" + _serverName + " CAP * LS :");
  } else if (subcommand == "END") {
  }
}

void Server::cmdPass(Client* client, const Message& msg) {
  if (client->isRegistered()) {  //< a second USER after 001 -> 462 · registration is once-only
    sendReply(client, ERR_ALREADYREGISTRED);
    return;
  }
  if (msg.params.empty()) {
    replyNeedMoreParams(client, "PASS");
    return;
  }
  client->setPassword(msg.fieldOr("password", 0));
  client->setPassSent(true);
}

void Server::cmdNick(Client* client, const Message& msg) {
  if (msg.params.empty()) {
    sendReply(client, ERR_NONICKNAMEGIVEN);
    return;
  }

  std::string nick = msg.fieldOr("newnick", 0);

  //< RFC 2812 1.2.1 bounds a nickname at nine octets, but says nothing about
  //< what a server does with a longer one, and the two readings differ:
  //< refuse it, or keep the first nine. We TRUNCATE, the way ircd always has.
  //< Refusing is defensible on paper and wrong in practice -- a client whose
  //< configured nick is ten characters can never connect at all, and the 432
  //< it gets back names a nickname it did not choose to be invalid.
  //< Order matters here: truncate FIRST, then validate. "1abcdefghij" must
  //< still be refused for its leading digit, not accepted because the cut
  //< happened to remove the offending tail -- it does not.
  if (nick.size() > Limits::kNickLen) nick.erase(Limits::kNickLen);

  if (!IrcName::isNickname(nick)) {  //< "1abc" "a.b" -> 432 · "z`tick" passes since D1 closed
    sendReply(client, ERR_ERRONEUSNICKNAME, nick);
    return;
  }

  if (isNickInUse(nick, client)) {
    sendReply(client, ERR_NICKNAMEINUSE, nick);
    return;
  }

  for (size_t i = 0; i < _extensions.size(); ++i) {
    if (_extensions[i]->reservesNick(nick)) {
      sendReply(client, ERR_NICKNAMEINUSE, nick);
      return;
    }
  }

  if (client->isRegistered()) {
    std::string oldPrefix = client->getPrefix();
    std::string nickMsg = IrcMessage::relay(oldPrefix, "NICK", "", nick);

    client->queueMessage(nickMsg);

    broadcastToChannels(client, nickMsg);

    client->setNickname(nick);
  } else {
    client->setNickname(nick);
    client->setNickSet(true);

    if (client->hasUser()) completeRegistration(client);
  }
}

/*
** RFC 2812 3.1.3: "The <mode> parameter should be a numeric".
**
** Digits, and nothing else. Not "+i", which is MODE syntax and not a bitmask.
** Not "-1", because the parameter is a bitmask and a bitmask has no sign. Not
** "0x10" or "0.5", which are numeric only to a C programmer.
**
** This is the check that makes USER's parameter POSITIONS mean something.
** USER takes <user> <mode> <unused> <realname>, and <unused> is ignored by
** every server -- so with no rule on <mode> there is nothing left to tell
** `USER dylan 0 * :R` from `USER dylan * 0 :R`. Both have four parameters and
** both have a valid username, and the second one would register happily with
** its arguments in the wrong order. Requiring <mode> to be numeric is what
** rejects it, and it is the only positional constraint the command has.
*/
static bool isUserModeParam(const std::string& param) {
  if (param.empty()) return false;
  for (std::string::size_type i = 0; i < param.size(); ++i)
    if (!IrcName::isAsciiDigit(param[i])) return false;
  return true;
}

/*
** Apply the bitmask. Only two bits signify: bit 2 (4) sets 'w', bit 3 (8)
** sets 'i'; RFC 2812 3.1.3 defines no others.
**
** Accumulated modulo 256 rather than parsed into a long, because <mode> is an
** arbitrarily long digit string by now and "99999999999999999999" would
** overflow. The modulus is a multiple of 16, so bits 2 and 3 survive it
** exactly -- the answer is the same one a wide-enough integer would give.
*/
static void applyUserModeBitmask(Client* client, const std::string& param) {
  unsigned int bits = 0;
  for (std::string::size_type i = 0; i < param.size(); ++i)
    bits = (bits * 10 + static_cast<unsigned int>(param[i] - '0')) % 256;

  if (bits & 4) client->setWallops(true);
  if (bits & 8) client->setInvisible(true);
}

void Server::cmdUser(Client* client, const Message& msg) {
  if (client->isRegistered()) {
    sendReply(client, ERR_ALREADYREGISTRED);
    return;
  }
  if (msg.params.size() < 4) {  //< "USER a 0 *" -> 461 · "USER a 0 * :R" is 4 and passes
    replyNeedMoreParams(client, "USER");
    return;
  }

  if (!msg.matched() && msg.trailingIndex != 3) {
    replyNeedMoreParams(client, "USER");
    return;
  }

  std::string username = msg.fieldOr("username", 0);
  if (username.size() > Limits::kUserLen) username.erase(Limits::kUserLen);
  if (!IrcName::isUsername(username)) {
    sendNumeric(client, ERR_NEEDMOREPARAMS, "USER :Invalid username");
    return;
  }
  client->setUsername(username);

  //< <mode> is checked after <user> so a line wrong in both ways reports the
  //< username first, matching the parameter order the client sent.
  if (!isUserModeParam(msg.params[1])) {
    sendNumeric(client, ERR_NEEDMOREPARAMS, "USER :<mode> must be numeric");
    return;
  }

  applyUserModeBitmask(client, msg.params[1]);

  client->setRealname(msg.fieldOr("realname", 3));
  client->setUserSet(true);

  if (client->hasNick()) completeRegistration(client);
}

void Server::completeRegistration(Client* client) {
  if (!client->hasPassSent() || !libcpp::str::eq_consttime(client->getPassword(), _password)) {
    sendReply(client, ERR_PASSWDMISMATCH);
    disconnectClient(client->getFd(), "Password mismatch");
    return;
  }

  client->setRegistered(true);

  std::string nick = client->getNickname();
  std::string prefix = client->getPrefix();

  sendNumeric(client, RPL_WELCOME, ":Welcome to the " + _serverName + " Network " + prefix);

  sendNumeric(client, RPL_YOURHOST, ":Your host is " + _serverName + ", running version " + settings().serverVersion);

  sendNumeric(client, RPL_CREATED, ":This server was created " + settings().serverCreated);

  sendNumeric(client, RPL_MYINFO, _serverName + " " + settings().serverVersion + " o itkol");

  sendNumeric(client, RPL_ISUPPORT,
              "CHANTYPES=# PREFIX=(o)@ CHANMODES=,,kl,it NICKLEN=" + libcpp::str::to_string(Limits::kNickLen) +
                  " CHANNELLEN=" + libcpp::str::to_string(Limits::kChannelLen) +
                  " TOPICLEN=" + libcpp::str::to_string(Limits::kTopicLen) + " NETWORK=" + _serverName +
                  " CASEMAPPING=ascii :are supported by this server");

  sendReply(client, ERR_NOMOTD);

  IrcTrace::sessionRegistered(client->getFd(), prefix);
  Log::success("registered " + nick + " (" + client->getUsername() + "@" + client->getHostname() + ")");

  for (size_t i = 0; i < _extensions.size(); ++i) _extensions[i]->onClientRegistered(*this, *client);
}
