#include <string>

#include "IrcCase.hpp"
#include "IrcTrace.hpp"
#include "Log.hpp"
#include "Server.hpp"
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
  if (client->isRegistered()) {
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

  if (!isValidNickname(nick)) {
    sendReply(client, ERR_ERRONEUSNICKNAME, nick);
    return;
  }

  if (nick.size() > MAX_NICKLEN) nick.erase(MAX_NICKLEN);

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
    std::string nickMsg = ":" + oldPrefix + " NICK :" + nick;

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
  if (msg.params.size() < 4) {
    replyNeedMoreParams(client, "USER");
    return;
  }

  if (msg.matched() ? false : msg.trailingIndex != 3) {
    replyNeedMoreParams(client, "USER");
    return;
  }

  std::string username = msg.matched() ? msg.field("username") : msg.params[0];
  if (username.size() > MAX_USERLEN) username.erase(MAX_USERLEN);
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

  sendNumeric(client, RPL_YOURHOST, ":Your host is " + _serverName + ", running version " + SERVER_VERSION);

  sendNumeric(client, RPL_CREATED, ":This server was created " + std::string(SERVER_CREATED));

  sendNumeric(client, RPL_MYINFO, _serverName + " " + SERVER_VERSION + " o itkol");

  sendNumeric(client, RPL_ISUPPORT,
              "CHANTYPES=# PREFIX=(o)@ CHANMODES=,,kl,it "
              "NICKLEN=9 CHANNELLEN=50 TOPICLEN=390 "
              "NETWORK=" +
                  _serverName +
                  " CASEMAPPING=ascii "
                  ":are supported by this server");

  sendReply(client, ERR_NOMOTD);

  IrcTrace::sessionRegistered(client->getFd(), prefix);
  Log::success("registered " + nick + " (" + client->getUsername() + "@" + client->getHostname() + ")");
  audit("register", nick, client->getUsername() + "@" + client->getHostname());

  for (size_t i = 0; i < _extensions.size(); ++i) _extensions[i]->onClientRegistered(*this, *client);
}
