/* ─── Operator commands: KICK, INVITE, TOPIC, MODE ─── */

#include <string>
#include <vector>

#include "IrcCase.hpp"
#include "Server.hpp"
#include "libcpp/str/format.hpp"

/* Answer each distinct complaint about a mode string once.
**
** A mode string is a list of letters, and the same letter -- or the same
** missing-parameter condition -- can occur many times in one command.
** Replying per occurrence turned a single 512-octet MODE into hundreds of
** lines: 495 unknown letters measured at 23 KB of replies (47x), and 495
** parameter-less 'o' at 46 KB (91x), all queued against the sender's own
** 64 KiB SENDQ. The client learns nothing from the repeats -- "j is unknown"
** is just as true the first time as the third.
**
** Returns true if `what` has been reported already; records it otherwise. */
static bool alreadyReported(std::vector<std::string>& seen,
                            const std::string& what) {
  for (size_t i = 0; i < seen.size(); ++i)
    if (seen[i] == what) return true;
  seen.push_back(what);
  return false;
}

/* How many parameters the modes from `from` onwards still require.
**
** Used to decide whether a bare "-k" may swallow the next parameter. '-k'
** itself is deliberately not counted: whether its argument is present is
** exactly the question this answers. */
static size_t paramsRequiredFrom(const std::string& modeStr, size_t from,
                                 bool sign) {
  size_t need = 0;
  bool adding = sign;
  for (size_t i = from; i < modeStr.size(); ++i) {
    char c = modeStr[i];
    if (c == '+') {
      adding = true;
    } else if (c == '-') {
      adding = false;
    } else if (c == 'o') {
      ++need;
    } else if ((c == 'k' || c == 'l') && adding) {
      ++need;
    }
  }
  return need;
}

/* A channel key must be short and contain no space, comma or control
** character — it travels as a single middle parameter of JOIN/MODE. */
static bool isValidChannelKey(const std::string& key) {
  if (key.empty() || key.size() > MAX_KEYLEN) return false;
  for (std::string::size_type i = 0; i < key.size(); ++i) {
    unsigned char c = static_cast<unsigned char>(key[i]);
    if (c <= ' ' || c == ',') return false;
  }
  return true;
}

/* ─── KICK ─── */

void Server::cmdKick(Client* client, const Message& msg) {
  if (msg.params.size() < 2 || msg.params[0].empty() || msg.params[1].empty()) {
    sendReply(client, ERR_NEEDMOREPARAMS, "KICK :Not enough parameters");
    return;
  }

  const std::string& chanName = msg.params[0];
  const std::string& target = msg.params[1];
  std::string reason = client->getNickname();
  if (msg.params.size() > 2) reason = msg.params[2];

  Channel* chan = findChannel(chanName);
  if (!chan) {
    sendReply(client, ERR_NOSUCHCHANNEL, chanName + " :No such channel");
    return;
  }

  if (!chan->isMember(client)) {
    sendReply(client, ERR_NOTONCHANNEL,
              chanName + " :You're not on that channel");
    return;
  }

  if (!chan->isOperator(client)) {
    sendReply(client, ERR_CHANOPRIVSNEEDED,
              chanName + " :You're not channel operator");
    return;
  }

  Client* targetClient = chan->findMember(target);
  if (!targetClient) {
    sendReply(client, ERR_USERNOTINCHANNEL,
              target + " " + chanName + " :They aren't on that channel");
    return;
  }

  /* Echo the canonical channel and nick, not the spelling the kicker
  ** happened to type: a client matching its channel/user lists by string
  ** desyncs when told "KICK #CHAN NICK" for its "#chan"/"nick". */
  std::string kickMsg = ":" + client->getPrefix() + " KICK " + chan->getName() +
                        " " + targetClient->getNickname() + " :" + reason;
  chan->broadcastMessage(kickMsg, NULL);
  chan->removeMember(targetClient);

  if (chan->isEmpty()) removeChannel(chanName);
}

/* ─── INVITE ─── */

void Server::cmdInvite(Client* client, const Message& msg) {
  if (msg.params.size() < 2 || msg.params[0].empty() || msg.params[1].empty()) {
    sendReply(client, ERR_NEEDMOREPARAMS, "INVITE :Not enough parameters");
    return;
  }

  const std::string& target = msg.params[0];
  const std::string& chanName = msg.params[1];

  Channel* chan = findChannel(chanName);
  if (!chan) {
    sendReply(client, ERR_NOSUCHCHANNEL, chanName + " :No such channel");
    return;
  }

  if (!chan->isMember(client)) {
    sendReply(client, ERR_NOTONCHANNEL,
              chanName + " :You're not on that channel");
    return;
  }

  if (chan->isInviteOnly() && !chan->isOperator(client)) {
    sendReply(client, ERR_CHANOPRIVSNEEDED,
              chanName + " :You're not channel operator");
    return;
  }

  Client* targetClient = findClientByNick(target);
  if (!targetClient) {
    sendReply(client, ERR_NOSUCHNICK, target + " :No such nick/channel");
    return;
  }

  if (chan->isMember(targetClient)) {
    sendReply(client, ERR_USERONCHANNEL,
              target + " " + chanName + " :is already on channel");
    return;
  }

  // Add to invite list (so they can bypass +i)
  chan->addInvite(targetClient);

  // Confirm to sender
  sendReply(client, RPL_INVITING, target + " " + chanName);

  // Notify target
  targetClient->queueMessage(":" + client->getPrefix() + " INVITE " + target +
                             " :" + chanName);
}

/* ─── TOPIC ─── */

void Server::cmdTopic(Client* client, const Message& msg) {
  if (msg.params.empty() || msg.params[0].empty()) {
    sendReply(client, ERR_NEEDMOREPARAMS, "TOPIC :Not enough parameters");
    return;
  }

  const std::string& chanName = msg.params[0];
  Channel* chan = findChannel(chanName);

  if (!chan) {
    sendReply(client, ERR_NOSUCHCHANNEL, chanName + " :No such channel");
    return;
  }

  if (!chan->isMember(client)) {
    sendReply(client, ERR_NOTONCHANNEL,
              chanName + " :You're not on that channel");
    return;
  }

  if (msg.params.size() == 1) {
    // Query topic
    if (chan->getTopic().empty()) {
      sendReply(client, RPL_NOTOPIC, chanName + " :No topic is set");
    } else {
      sendReply(client, RPL_TOPIC, chanName + " :" + chan->getTopic());
      sendReply(client, RPL_TOPICWHOTIME,
                chanName + " " + chan->getTopicSetter() + " " +
                    libcpp::str::to_string(chan->getTopicTime()));
    }
    return;
  }

  // Set topic
  if (chan->isTopicRestricted() && !chan->isOperator(client)) {
    sendReply(client, ERR_CHANOPRIVSNEEDED,
              chanName + " :You're not channel operator");
    return;
  }

  /* Enforce the TOPICLEN=390 advertised in 005: truncate, don't reject. */
  std::string newTopic = msg.params[1];
  if (newTopic.size() > MAX_TOPICLEN) newTopic.erase(MAX_TOPICLEN);
  chan->setTopic(newTopic, client->getNickname());

  chan->broadcastMessage(
      ":" + client->getPrefix() + " TOPIC " + chan->getName() + " :" + newTopic,
      NULL);
}

/* ─── MODE ─── */

void Server::cmdMode(Client* client, const Message& msg) {
  /* Guarding the empty case also makes target[0] below a plain in-range
  ** read rather than leaning on C++98's const operator[] returning
  ** charT() at pos == size(). */
  if (msg.params.empty() || msg.params[0].empty()) {
    sendReply(client, ERR_NEEDMOREPARAMS, "MODE :Not enough parameters");
    return;
  }

  const std::string& target = msg.params[0];

  if (target[0] == '#') {
    // Channel mode
    Channel* chan = findChannel(target);
    if (!chan) {
      sendReply(client, ERR_NOSUCHCHANNEL, target + " :No such channel");
      return;
    }
    if (msg.params.size() == 1) {
      /* Mode query. Membership is required here for the same reason
      ** every mode *change* requires it: RPL_CHANNELMODEIS carries
      ** the +k key as a parameter, so an unguarded read path hands
      ** the channel password to any stranger who asks and defeats
      ** +k entirely. */
      if (!chan->isMember(client)) {
        sendReply(client, ERR_NOTONCHANNEL,
                  target + " :You're not on that channel");
        return;
      }
      std::string modes = chan->getModeString();
      std::string params = chan->getModeParams();
      std::string reply = target + " " + modes;
      if (!params.empty()) reply += " " + params;
      sendReply(client, RPL_CHANNELMODEIS, reply);

      sendReply(client, RPL_CREATIONTIME,
                target + " " + libcpp::str::to_string(chan->getCreationTime()));
      return;
    }
    handleChannelMode(client, chan, msg);
  } else {
    // User mode
    handleUserMode(client, msg);
  }
}

/* ─── User Mode Handler ─── */

void Server::handleUserMode(Client* client, const Message& msg) {
  const std::string& target = msg.params[0];

  // Can only query/change your own modes (CASEMAPPING=ascii, so the
  // client may name itself in any case -- never a raw string compare)
  if (!ircEquals(target, client->getNickname())) {
    sendReply(client, ERR_USERSDONTMATCH, ":Can't change mode for other users");
    return;
  }

  if (msg.params.size() == 1) {
    // Query mode
    sendReply(client, RPL_UMODEIS, "+");
    return;
  }

  /* No user modes are supported. HexChat sends "MODE <nick> +i" right
  ** after registering and treats silence as success, so accept it without
  ** erroring rather than rejecting a mode we simply don't track. */
}

/* ─── Channel Mode Handler ─── */

void Server::handleChannelMode(Client* client, Channel* channel,
                               const Message& msg) {
  if (!channel->isMember(client)) {
    sendReply(client, ERR_NOTONCHANNEL,
              channel->getName() + " :You're not on that channel");
    return;
  }

  if (!channel->isOperator(client)) {
    sendReply(client, ERR_CHANOPRIVSNEEDED,
              channel->getName() + " :You're not channel operator");
    return;
  }

  const std::string& modeStr = msg.params[1];
  bool adding = true;
  size_t paramIdx = 2;

  std::string appliedModes;
  std::string appliedParams;
  bool currentSign = true;  // true = +

  /* One entry per distinct complaint already answered for this command --
  ** see alreadyReported(). */
  std::vector<std::string> reported;

  for (size_t i = 0; i < modeStr.size(); ++i) {
    char c = modeStr[i];

    if (c == '+') {
      adding = true;
      continue;
    }
    if (c == '-') {
      adding = false;
      continue;
    }

    switch (c) {
      case 'i': {
        channel->setInviteOnly(adding);
        if (adding != currentSign || appliedModes.empty()) {
          appliedModes += (adding ? "+" : "-");
          currentSign = adding;
        }
        appliedModes += "i";
        break;
      }
      case 't': {
        channel->setTopicRestricted(adding);
        if (adding != currentSign || appliedModes.empty()) {
          appliedModes += (adding ? "+" : "-");
          currentSign = adding;
        }
        appliedModes += "t";
        break;
      }
      case 'k': {
        if (adding) {
          if (paramIdx >= msg.params.size()) {
            if (!alreadyReported(reported, "461"))
              sendReply(client, ERR_NEEDMOREPARAMS,
                        "MODE :Not enough parameters");
            continue;
          }
          std::string key = msg.params[paramIdx++];
          if (!isValidChannelKey(key)) {
            if (!alreadyReported(reported, "525:" + key))
              sendReply(client, ERR_INVALIDKEY,
                        channel->getName() + " :Key is not well-formed");
            continue;
          }
          channel->setKey(key);
          if (adding != currentSign || appliedModes.empty()) {
            appliedModes += "+";
            currentSign = true;
          }
          appliedModes += "k";
          if (!appliedParams.empty()) appliedParams += " ";
          appliedParams += key;
        } else {
          channel->removeKey();
          if (adding != currentSign || appliedModes.empty()) {
            appliedModes += "-";
            currentSign = false;
          }
          appliedModes += "k";
          /* RFC 2812 spells -k with the key as an argument and HexChat
          ** sends one, but a bare "-k" is just as common, so the argument
          ** is treated as optional. Take it only when it is SURPLUS to
          ** what the modes after this one still need.
          **
          ** Consuming it unconditionally is what "MODE #c -k+o nick" used
          ** to do: -k swallowed `nick` as a key it then discarded, +o found
          ** no parameter left and answered 461, and the operator grant
          ** silently never happened -- while the broadcast said plain "-k",
          ** showing no sign that a parameter had been eaten. */
          size_t stillNeeded = paramsRequiredFrom(modeStr, i + 1, adding);
          if (paramIdx < msg.params.size() &&
              msg.params.size() - paramIdx > stillNeeded) {
            /* Echo it back. A consumed parameter that does not appear in
            ** the broadcast is indistinguishable from one that was never
            ** sent. */
            std::string oldKey = msg.params[paramIdx++];
            if (!appliedParams.empty()) appliedParams += " ";
            appliedParams += oldKey;
          }
        }
        break;
      }
      case 'o': {
        if (paramIdx >= msg.params.size()) {
          if (!alreadyReported(reported, "461"))
            sendReply(client, ERR_NEEDMOREPARAMS,
                      "MODE :Not enough parameters");
          continue;
        }
        std::string nick = msg.params[paramIdx++];
        Client* target = channel->findMember(nick);
        if (!target) {
          if (!alreadyReported(reported, "441:" + ircToLower(nick)))
            sendReply(client, ERR_USERNOTINCHANNEL,
                      nick + " " + channel->getName() +
                          " :They aren't on that channel");
          continue;
        }
        channel->setOperator(target, adding);
        if (adding != currentSign || appliedModes.empty()) {
          appliedModes += (adding ? "+" : "-");
          currentSign = adding;
        }
        appliedModes += "o";
        if (!appliedParams.empty()) appliedParams += " ";
        /* Canonical nick, not the operator's spelling. */
        appliedParams += target->getNickname();
        break;
      }
      case 'l': {
        if (adding) {
          if (paramIdx >= msg.params.size()) {
            if (!alreadyReported(reported, "461"))
              sendReply(client, ERR_NEEDMOREPARAMS,
                        "MODE :Not enough parameters");
            continue;
          }
          std::string limitStr = msg.params[paramIdx++];
          /* Full-string, range-checked: rejects "12abc", " 12",
          ** "+12", negatives and anything that overflows -- and
          ** leaves errno untouched, which the raw strtol idiom
          ** here did not. */
          long limit = 0;
          if (!libcpp::str::parse_long(limitStr, 1, MAX_USERLIMIT, limit)) {
            /* Say so instead of dropping it: silence is
            ** indistinguishable from success to the operator
            ** who sent it. +k has a dedicated numeric
            ** (ERR_INVALIDKEY); +l has none, so use the
            ** generic mode-parameter one. */
            if (!alreadyReported(reported, "696:" + limitStr))
              sendReply(client, ERR_INVALIDMODEPARAM,
                        channel->getName() + " l " + limitStr +
                            " :Invalid channel limit");
            continue;
          }
          channel->setUserLimit(static_cast<size_t>(limit));
          if (adding != currentSign || appliedModes.empty()) {
            appliedModes += "+";
            currentSign = true;
          }
          appliedModes += "l";
          if (!appliedParams.empty()) appliedParams += " ";
          /* Echo the canonical parsed number, not the raw text. */
          appliedParams += libcpp::str::to_string(limit);
        } else {
          channel->removeUserLimit();
          if (adding != currentSign || appliedModes.empty()) {
            appliedModes += "-";
            currentSign = false;
          }
          appliedModes += "l";
        }
        break;
      }
      default: {
        std::string s(1, c);
        if (!alreadyReported(reported, "472:" + s))
          sendReply(client, ERR_UNKNOWNMODE,
                    s + " :is unknown mode char to me");
        break;
      }
    }
  }

  // Broadcast applied modes
  if (!appliedModes.empty()) {
    std::string modeMsg = ":" + client->getPrefix() + " MODE " +
                          channel->getName() + " " + appliedModes;
    if (!appliedParams.empty()) modeMsg += " " + appliedParams;
    channel->broadcastMessage(modeMsg, NULL);
  }
}
