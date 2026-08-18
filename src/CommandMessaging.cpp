/* ─── Messaging commands: PRIVMSG, NOTICE, PING, PONG, QUIT ─── */

#include <string>
#include <vector>

#include "IrcCase.hpp"
#include "Server.hpp"
#include "ext/IServerExtension.hpp"
#include "libcpp/str/format.hpp"

/* ─── PRIVMSG ─── */

void Server::cmdPrivmsg(Client* client, const Message& msg) {
  /* An empty target is a missing target, not something to interpolate
  ** into a numeric (where it collapses two spaces and shifts the
  ** parameter list for a strict parser). */
  if (msg.params.empty() || msg.params[0].empty()) {
    sendReply(client, ERR_NORECIPIENT, ":No recipient given (PRIVMSG)");
    return;
  }
  /* A trailing ":" parses to a present-but-empty second parameter, so a
  ** bare size check lets "PRIVMSG #chan :" through and relays an empty
  ** message. RFC 2812 wants 412 here. */
  if (msg.params.size() < 2 || msg.params[1].empty()) {
    sendReply(client, ERR_NOTEXTTOSEND, ":No text to send");
    return;
  }

  const std::string& text = msg.params[1];

  std::vector<std::string> targets =
      libcpp::str::split_nonempty(msg.params[0], ',');
  for (size_t t = 0; t < targets.size(); ++t) {
    const std::string& target = targets[t];

    if (target[0] == '#') {
      // Channel message
      Channel* chan = findChannel(target);
      if (!chan) {
        sendReply(client, ERR_NOSUCHCHANNEL, target + " :No such channel");
        continue;
      }
      if (!chan->isMember(client)) {
        sendReply(client, ERR_CANNOTSENDTOCHAN,
                  target + " :Cannot send to channel");
        continue;
      }
      chan->broadcastMessage(
          ":" + client->getPrefix() + " PRIVMSG " + target + " :" + text,
          client);
    } else {
      // A virtual participant (bot, service) may claim this target
      bool handled = false;
      for (size_t k = 0; k < _extensions.size() && !handled; ++k)
        handled = _extensions[k]->onPrivmsg(*this, *client, target, text);
      if (handled) continue;

      // Private message to user
      Client* dest = findClientByNick(target);
      if (!dest) {
        sendReply(client, ERR_NOSUCHNICK, target + " :No such nick/channel");
        continue;
      }
      dest->queueMessage(":" + client->getPrefix() + " PRIVMSG " + target +
                         " :" + text);
    }
  }
}

/* ─── NOTICE ─── */

void Server::cmdNotice(Client* client, const Message& msg) {
  // NOTICE is like PRIVMSG but must never generate automatic replies --
  // so an empty text is dropped silently rather than drawing a 412.
  if (msg.params.size() < 2 || msg.params[0].empty() || msg.params[1].empty())
    return;

  const std::string& text = msg.params[1];

  std::vector<std::string> targets =
      libcpp::str::split_nonempty(msg.params[0], ',');
  for (size_t t = 0; t < targets.size(); ++t) {
    const std::string& target = targets[t];

    if (target[0] == '#') {
      Channel* chan = findChannel(target);
      if (!chan || !chan->isMember(client)) continue;
      chan->broadcastMessage(
          ":" + client->getPrefix() + " NOTICE " + target + " :" + text,
          client);
    } else {
      Client* dest = findClientByNick(target);
      if (!dest) continue;
      dest->queueMessage(":" + client->getPrefix() + " NOTICE " + target +
                         " :" + text);
    }
  }
}

/* ─── PING ─── */

void Server::cmdPing(Client* client, const Message& msg) {
  if (msg.params.empty()) {
    sendReply(client, ERR_NEEDMOREPARAMS, "PING :Not enough parameters");
    return;
  }
  client->queueMessage(":" + _serverName + " PONG " + _serverName + " :" +
                       msg.params[0]);
}

/* ─── PONG ─── */

void Server::cmdPong(Client* client, const Message& msg) {
  (void)msg;
  client->updateLastActivity();
  client->setPingSent(false);
}

/* ─── QUIT ─── */

void Server::cmdQuit(Client* client, const Message& msg) {
  std::string reason = "Quit";
  if (!msg.params.empty()) reason = msg.params[0];

  int fd = client->getFd();
  disconnectClient(fd, reason);
}
