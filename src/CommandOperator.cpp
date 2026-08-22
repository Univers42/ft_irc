#include <string>
#include <vector>

#include "IrcCase.hpp"
#include "Server.hpp"
#include "libcpp/str/format.hpp"

static bool alreadyReported(std::vector<std::string>& seen,
                            const std::string& what) {
  for (size_t i = 0; i < seen.size(); ++i)
    if (seen[i] == what) return true;
  seen.push_back(what);
  return false;
}

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

static bool isValidChannelKey(const std::string& key) {
  if (key.empty() || key.size() > MAX_KEYLEN) return false;
  for (std::string::size_type i = 0; i < key.size(); ++i) {
    unsigned char c = static_cast<unsigned char>(key[i]);
    if (c <= ' ' || c == ',') return false;
  }
  return true;
}

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

  std::string kickMsg = ":" + client->getPrefix() + " KICK " + chan->getName() +
                        " " + targetClient->getNickname() + " :" + reason;
  chan->broadcastMessage(kickMsg, NULL);
  chan->removeMember(targetClient);

  if (chan->isEmpty()) removeChannel(chanName);
}

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

  chan->addInvite(targetClient);

  sendReply(client, RPL_INVITING, target + " " + chanName);

  targetClient->queueMessage(":" + client->getPrefix() + " INVITE " + target +
                             " :" + chanName);
}

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

  if (chan->isTopicRestricted() && !chan->isOperator(client)) {
    sendReply(client, ERR_CHANOPRIVSNEEDED,
              chanName + " :You're not channel operator");
    return;
  }

  std::string newTopic = msg.params[1];
  if (newTopic.size() > MAX_TOPICLEN) newTopic.erase(MAX_TOPICLEN);
  chan->setTopic(newTopic, client->getNickname());

  chan->broadcastMessage(
      ":" + client->getPrefix() + " TOPIC " + chan->getName() + " :" + newTopic,
      NULL);
}

void Server::cmdMode(Client* client, const Message& msg) {
  if (msg.params.empty() || msg.params[0].empty()) {
    sendReply(client, ERR_NEEDMOREPARAMS, "MODE :Not enough parameters");
    return;
  }

  const std::string& target = msg.params[0];

  if (target[0] == '#') {
    Channel* chan = findChannel(target);
    if (!chan) {
      sendReply(client, ERR_NOSUCHCHANNEL, target + " :No such channel");
      return;
    }
    if (msg.params.size() == 1) {
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
    handleUserMode(client, msg);
  }
}

void Server::handleUserMode(Client* client, const Message& msg) {
  const std::string& target = msg.params[0];

  if (!ircEquals(target, client->getNickname())) {
    sendReply(client, ERR_USERSDONTMATCH, ":Can't change mode for other users");
    return;
  }

  if (msg.params.size() == 1) {
    sendReply(client, RPL_UMODEIS, "+");
    return;
  }
}

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
  bool currentSign = true;

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

          size_t stillNeeded = paramsRequiredFrom(modeStr, i + 1, adding);
          if (paramIdx < msg.params.size() &&
              msg.params.size() - paramIdx > stillNeeded) {
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

          long limit = 0;
          if (!libcpp::str::parse_long(limitStr, 1, MAX_USERLIMIT, limit)) {
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

  if (!appliedModes.empty()) {
    std::string modeMsg = ":" + client->getPrefix() + " MODE " +
                          channel->getName() + " " + appliedModes;
    if (!appliedParams.empty()) modeMsg += " " + appliedParams;
    channel->broadcastMessage(modeMsg, NULL);
  }
}
