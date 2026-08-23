#include <string>

#include "IrcCase.hpp"
#include "IrcMessage.hpp"
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
  client->setPassword(msg.matched() ? msg.field("password") : msg.params[0]);
  client->setPassSent(true);
}

void Server::cmdNick(Client* client, const Message& msg) {
  if (msg.params.empty()) {
    sendReply(client, ERR_NONICKNAMEGIVEN);
    return;
  }

  std::string nick = msg.matched() ? msg.field("newnick") : msg.params[0];

  if (!isValidNickname(nick)) {  //< "1abc" "a.b" -> 432 · "z`tick" passes since D1 closed
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

static bool isValidUsernameChar(unsigned char c) {
  return (c >= 0x01 && c <= 0x09) || (c >= 0x0B && c <= 0x0C) || (c >= 0x0E && c <= 0x1F) || (c >= 0x21 && c <= 0x3F) ||
         (c >= 0x41);
}

static bool isValidUsername(const std::string& user) {
  if (user.empty()) return false;
  for (std::string::size_type i = 0; i < user.size(); ++i)
    if (!isValidUsernameChar(static_cast<unsigned char>(user[i]))) return false;
  return true;
}

static void applyUserModeBitmask(Client* client, const std::string& param) {
  long bits = 0;
  if (!libcpp::str::parse_long(param, 0, 255, bits)) return;

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

  if (msg.matched() ? false : msg.trailingIndex != 3) {
    replyNeedMoreParams(client, "USER");
    return;
  }

  std::string username = msg.matched() ? msg.field("username") : msg.params[0];
  if (username.size() > Limits::kUserLen) username.erase(Limits::kUserLen);
  if (!isValidUsername(username)) {
    sendNumeric(client, ERR_NEEDMOREPARAMS, "USER :Invalid username");
    return;
  }
  client->setUsername(username);

  applyUserModeBitmask(client, msg.params[1]);

  client->setRealname(msg.matched() ? msg.field("realname") : msg.params[3]);
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
  audit("register", nick, client->getUsername() + "@" + client->getHostname());

  for (size_t i = 0; i < _extensions.size(); ++i) _extensions[i]->onClientRegistered(*this, *client);
}
