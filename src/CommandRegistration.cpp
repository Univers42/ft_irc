/* ─── Registration commands: CAP, PASS, NICK, USER ─── */

#include <string>

#include "IrcCase.hpp"
#include "Log.hpp"
#include "IrcTrace.hpp"
#include "Server.hpp"
#include "ext/IServerExtension.hpp"
#include "libcpp/str/case.hpp"
#include "libcpp/str/secure.hpp"

/* ─── CAP ─── */

void Server::cmdCap(Client* client, const Message& msg) {
  if (msg.params.empty()) return;

  std::string subcommand = libcpp::str::to_upper(msg.params[0]);

  if (subcommand == "LS") {
    // No capabilities supported — send empty list
    client->queueMessage(":" + _serverName + " CAP * LS :");
  } else if (subcommand == "END") {
    // Nothing to do — registration proceeds normally
  }
  // Ignore other CAP subcommands (REQ, etc.)
}

/* ─── PASS ─── */

void Server::cmdPass(Client* client, const Message& msg) {
  if (client->isRegistered()) {
    sendReply(client, ERR_ALREADYREGISTRED, ":You may not reregister");
    return;
  }
  if (msg.params.empty()) {
    sendReply(client, ERR_NEEDMOREPARAMS, "PASS :Not enough parameters");
    return;
  }
  client->setPassword(msg.params[0]);
  client->setPassSent(true);
}

/* ─── NICK ─── */

void Server::cmdNick(Client* client, const Message& msg) {
  if (msg.params.empty()) {
    sendReply(client, ERR_NONICKNAMEGIVEN, ":No nickname given");
    return;
  }

  std::string nick = msg.params[0];

  if (!isValidNickname(nick)) {
    sendReply(client, ERR_ERRONEUSNICKNAME, nick + " :Erroneous nickname");
    return;
  }

  /* Truncate rather than reject past NICKLEN, like real ircds -- HexChat's
  ** own retry suffixes only make an over-long nick longer, so rejecting it
  ** leaves the client unable to connect at all. Character validation above
  ** already ran on the untruncated string, so a nick that's only invalid
  ** past position 9 still gets 432. This must happen before the collision
  ** check below: two different over-long nicks can truncate to the same
  ** name, and isNickInUse must compare the truncated form so the second
  ** one gets 433 instead of silently colliding. */
  if (nick.size() > MAX_NICKLEN) nick.erase(MAX_NICKLEN);

  /* Check if the nickname is taken (CASEMAPPING=ascii: "Bob" collides
  ** "bob"). Uses isNickInUse(), not findClientByNick(): a connection that
  ** has sent NICK but not yet PASS/USER still owns the name, and must
  ** block it, even though it is not addressable. */
  if (isNickInUse(nick, client)) {
    sendReply(client, ERR_NICKNAMEINUSE, nick + " :Nickname is already in use");
    return;
  }

  // Reject nicks reserved by extensions (bot, virtual participants)
  for (size_t i = 0; i < _extensions.size(); ++i) {
    if (_extensions[i]->reservesNick(nick)) {
      sendReply(client, ERR_NICKNAMEINUSE,
                nick + " :Nickname is already in use");
      return;
    }
  }

  if (client->isRegistered()) {
    // Nick change — broadcast to all shared channels
    std::string oldPrefix = client->getPrefix();
    std::string nickMsg = ":" + oldPrefix + " NICK :" + nick;

    // Send to the client itself
    client->queueMessage(nickMsg);
    // Send to all users sharing a channel
    broadcastToChannels(client, nickMsg);

    client->setNickname(nick);
  } else {
    client->setNickname(nick);
    client->setNickSet(true);

    // Check if registration can be completed
    if (client->hasUser()) completeRegistration(client);
  }
}

/* ─── USER ─── */

void Server::cmdUser(Client* client, const Message& msg) {
  if (client->isRegistered()) {
    sendReply(client, ERR_ALREADYREGISTRED, ":You may not reregister");
    return;
  }
  if (msg.params.size() < 4) {
    sendReply(client, ERR_NEEDMOREPARAMS, "USER :Not enough parameters");
    return;
  }

  /* Bound the username: it is echoed in the prefix of every line this
  ** client's traffic is relayed under, so an oversized one would push the
  ** actual payload past the 512-byte line cap. Truncate rather than
  ** reject -- that is what ircds do, and rejecting would fail
  ** registration over a cosmetic field. */
  std::string username = msg.params[0];
  if (username.size() > MAX_USERLEN) username.erase(MAX_USERLEN);
  client->setUsername(username);
  // params[1] = mode (ignored), params[2] = unused
  /* ponytail: realname is left unbounded. Unlike the username it never
  ** enters a message prefix -- it appears only in the 311/352 query
  ** replies, where an overlong value costs a truncated reply and nothing
  ** else. Upgrade path if that ever shows: truncate here like the
  ** username above. */
  client->setRealname(msg.params[3]);
  client->setUserSet(true);

  if (client->hasNick()) completeRegistration(client);
}

/* ─── Complete Registration ─── */

void Server::completeRegistration(Client* client) {
  // Check password (timing-safe: comparison cost never leaks a prefix)
  if (!client->hasPassSent() ||
      !libcpp::str::eq_consttime(client->getPassword(), _password)) {
    sendReply(client, ERR_PASSWDMISMATCH, ":Password incorrect");
    disconnectClient(client->getFd(), "Password mismatch");
    return;
  }

  client->setRegistered(true);

  std::string nick = client->getNickname();
  std::string prefix = client->getPrefix();

  // 001 RPL_WELCOME
  sendReply(client, RPL_WELCOME,
            ":Welcome to the " + _serverName + " Network " + prefix);

  // 002 RPL_YOURHOST
  sendReply(
      client, RPL_YOURHOST,
      ":Your host is " + _serverName + ", running version " + SERVER_VERSION);

  // 003 RPL_CREATED
  sendReply(client, RPL_CREATED,
            ":This server was created " + std::string(SERVER_CREATED));

  // 004 RPL_MYINFO
  sendReply(client, RPL_MYINFO,
            _serverName + " " + SERVER_VERSION + " o itkol");

  /* 005 RPL_ISUPPORT
  **
  ** CHANMODES groups are A,B,C,D: A = list modes (always a parameter),
  ** B = always a parameter both ways, C = a parameter only when set,
  ** D = never a parameter.
  **
  ** `l` lives in C, not D: "+l 20" carries the limit while "-l" does not.
  ** It was advertised in D, which told a client that MODE #chan +l takes no
  ** argument -- the one client that believed the token would mis-parse
  ** every +l broadcast this server sends.
  **
  ** `k` is in C rather than the conventional B, because this server's "-k"
  ** does NOT require an argument: handleChannelMode() takes one only when
  ** it is surplus to what the modes after it need. Advertising B was tried
  ** and is observably wrong -- HexChat believes the token, so it read the
  ** broadcast ":alice MODE #general -k+o bob" as "-k bob" plus a "+o" with
  ** no argument and rendered "alice gives channel operator status to "
  ** with an empty nick, while the server had correctly opped bob. ISUPPORT
  ** cannot say "optional", so C -- no argument on removal -- is the group
  ** that matches what this server actually sends. */
  sendReply(client, RPL_ISUPPORT,
            "CHANTYPES=# PREFIX=(o)@ CHANMODES=,,kl,it "
            "NICKLEN=9 CHANNELLEN=50 TOPICLEN=390 "
            "NETWORK=" +
                _serverName +
                " CASEMAPPING=ascii "
                ":are supported by this server");

  // 422 ERR_NOMOTD (we don't have a MOTD file)
  sendReply(client, ERR_NOMOTD, ":MOTD File is missing");

  IrcTrace::sessionRegistered(client->getFd(), prefix);
  Log::success("registered " + nick + " (" + client->getUsername() + "@" +
               client->getHostname() + ")");
  audit("register", nick, client->getUsername() + "@" + client->getHostname());

  for (size_t i = 0; i < _extensions.size(); ++i)
    _extensions[i]->onClientRegistered(*this, *client);
}
