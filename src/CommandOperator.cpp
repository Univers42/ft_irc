#include <string>
#include <vector>

#include "ChannelModes.hpp"
#include "IrcCase.hpp"
#include "Server.hpp"
#include "libcpp/str/format.hpp"

static bool alreadyReported(std::vector<std::string>& seen, const std::string& what) {
  for (size_t i = 0; i < seen.size(); ++i)
    if (seen[i] == what) return true;
  seen.push_back(what);
  return false;
}

namespace {
struct ModeChange {
  bool adding;
  char letter;
  std::string param;

  ModeChange(bool add, char c) : adding(add), letter(c), param() {}
  ModeChange(bool add, char c, const std::string& p) : adding(add), letter(c), param(p) {}
};
}  // namespace

static std::vector<std::string> renderModeLines(const std::string& head, const std::vector<ModeChange>& applied) {
  std::vector<std::string> lines;
  const size_t budget = static_cast<size_t>(MAX_MSGLEN) - 2;

  size_t i = 0;
  while (i < applied.size()) {
    std::string modes;
    std::string params;
    bool haveSign = false;
    bool sign = true;
    size_t taken = 0;

    while (i < applied.size()) {
      const ModeChange& mc = applied[i];

      std::string nextModes = modes;
      if (!haveSign || mc.adding != sign) nextModes += (mc.adding ? '+' : '-');
      nextModes += mc.letter;

      std::string nextParams = params;
      if (!mc.param.empty()) {
        if (!nextParams.empty()) nextParams += " ";
        nextParams += mc.param;
      }

      size_t len = head.size() + nextModes.size();
      if (!nextParams.empty()) len += 1 + nextParams.size();

      if (taken > 0 && len > budget) break;

      modes = nextModes;
      params = nextParams;
      haveSign = true;
      sign = mc.adding;
      ++taken;
      ++i;
    }

    std::string line = head + modes;
    if (!params.empty()) line += " " + params;
    lines.push_back(line);
  }
  return lines;
}

static void broadcastModeChanges(Channel* channel, const std::string& prefix, const std::vector<ModeChange>& applied) {
  const std::string head = ":" + prefix + " MODE " + channel->getName() + " ";
  const std::vector<std::string> lines = renderModeLines(head, applied);
  for (size_t i = 0; i < lines.size(); ++i) channel->broadcastMessage(lines[i], NULL);
}

void Server::cmdKick(Client* client, const Message& msg) {
  if (!msg.matched()) {
    replyNeedMoreParams(client, "KICK");
    return;
  }

  const std::string& chanName = msg.field("kickchans");
  const std::string& target = msg.field("kickusers");
  std::string reason = client->getNickname();
  if (msg.has("kickreason")) reason = msg.field("kickreason");

  Channel* chan = requireChannel(client, chanName);
  if (!chan) return;
  if (!requireMember(client, chan, chanName)) return;
  if (!requireChanOp(client, chan, chanName)) return;

  Client* targetClient = chan->findMember(target);
  if (!targetClient) {
    sendReply(client, ERR_USERNOTINCHANNEL, target + " " + chanName + " :They aren't on that channel");
    return;
  }

  std::string kickMsg =
      ":" + client->getPrefix() + " KICK " + chan->getName() + " " + targetClient->getNickname() + " :" + reason;
  chan->broadcastMessage(kickMsg, NULL);
  chan->removeMember(targetClient);

  if (chan->isEmpty()) removeChannel(chanName);
}

void Server::cmdInvite(Client* client, const Message& msg) {
  if (!msg.matched()) {
    replyNeedMoreParams(client, "INVITE");
    return;
  }

  const std::string& target = msg.field("invnick");
  const std::string& chanName = msg.field("invchan");

  Channel* chan = requireChannel(client, chanName);
  if (!chan) return;
  if (!requireMember(client, chan, chanName)) return;
  if (chan->isInviteOnly() && !requireChanOp(client, chan, chanName)) return;

  Client* targetClient = findClientByNick(target);
  if (!targetClient) {
    sendReply(client, ERR_NOSUCHNICK, target + " :No such nick/channel");
    return;
  }

  if (chan->isMember(targetClient)) {
    sendReply(client, ERR_USERONCHANNEL, target + " " + chanName + " :is already on channel");
    return;
  }

  chan->addInvite(targetClient);

  sendReply(client, RPL_INVITING, target + " " + chanName);

  targetClient->queueMessage(":" + client->getPrefix() + " INVITE " + target + " :" + chanName);
}

void Server::cmdTopic(Client* client, const Message& msg) {
  if (msg.params.empty() || msg.params[0].empty()) {
    replyNeedMoreParams(client, "TOPIC");
    return;
  }

  const std::string& chanName = msg.params[0];
  Channel* chan = requireChannel(client, chanName);
  if (!chan) return;
  if (!requireMember(client, chan, chanName)) return;

  if (msg.params.size() == 1) {
    if (chan->getTopic().empty()) {
      sendReply(client, RPL_NOTOPIC, chanName + " :No topic is set");
    } else {
      sendReply(client, RPL_TOPIC, chanName + " :" + chan->getTopic());
      sendReply(client, RPL_TOPICWHOTIME,
                chanName + " " + chan->getTopicSetter() + " " + libcpp::str::to_string(chan->getTopicTime()));
    }
    return;
  }

  if (chan->isTopicRestricted() && !requireChanOp(client, chan, chanName)) return;

  std::string newTopic = msg.matched() ? msg.field("topictext") : msg.params[1];
  if (newTopic.size() > MAX_TOPICLEN) newTopic.erase(MAX_TOPICLEN);
  chan->setTopic(newTopic, client->getNickname());

  chan->broadcastMessage(":" + client->getPrefix() + " TOPIC " + chan->getName() + " :" + newTopic, NULL);
}

void Server::cmdMode(Client* client, const Message& msg) {
  if (msg.params.empty() || msg.params[0].empty()) {
    replyNeedMoreParams(client, "MODE");
    return;
  }

  const std::string& target = msg.params[0];

  if (target[0] == '#') {
    Channel* chan = requireChannel(client, target);
    if (!chan) return;
    if (msg.params.size() == 1) {
      if (!requireMember(client, chan, target)) return;
      std::string modes = chan->getModeString();
      std::string params = chan->getModeParams();
      std::string reply = target + " " + modes;
      if (!params.empty()) reply += " " + params;
      sendReply(client, RPL_CHANNELMODEIS, reply);

      sendReply(client, RPL_CREATIONTIME, target + " " + libcpp::str::to_string(chan->getCreationTime()));
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
    sendReply(client, RPL_UMODEIS, client->getUserModeString());
    return;
  }

  const std::string& modeStr = msg.params[1];

  if (modeStr.empty() || (modeStr[0] != '+' && modeStr[0] != '-')) return;

  bool adding = true;
  std::vector<ModeChange> applied;
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

    if (c == 'i') {
      client->setInvisible(adding);
      applied.push_back(ModeChange(adding, 'i'));
    } else if (c == 'w') {
      client->setWallops(adding);
      applied.push_back(ModeChange(adding, 'w'));
    } else {
      std::string s(1, c);
      if (!alreadyReported(reported, "501:" + s))
        sendReply(client, ERR_UMODEUNKNOWNFLAG, s + " :is unknown mode char to me");
    }
  }

  if (!applied.empty()) {
    const std::string head = ":" + client->getPrefix() + " MODE " + client->getNickname() + " ";
    const std::vector<std::string> lines = renderModeLines(head, applied);
    for (size_t i = 0; i < lines.size(); ++i) client->queueMessage(lines[i]);
  }
}

void Server::handleChannelMode(Client* client, Channel* channel, const Message& msg) {
  if (!requireMember(client, channel, channel->getName())) return;
  if (!requireChanOp(client, channel, channel->getName())) return;

  const std::string& modeStr = msg.params[1];

  if (modeStr.empty() || (modeStr[0] != '+' && modeStr[0] != '-')) return;

  bool adding = true;
  size_t paramIdx = 2;

  std::vector<ModeChange> applied;
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
        applied.push_back(ModeChange(adding, 'i'));
        break;
      }
      case 't': {
        channel->setTopicRestricted(adding);
        applied.push_back(ModeChange(adding, 't'));
        break;
      }
      case 'k': {
        if (adding) {
          if (paramIdx >= msg.params.size()) {
            if (!alreadyReported(reported, "461")) replyNeedMoreParams(client, "MODE");
            continue;
          }
          std::string key = msg.params[paramIdx++];
          if (!isValidChannelKey(key)) {
            if (!alreadyReported(reported, "525:" + key))
              sendReply(client, ERR_INVALIDKEY, channel->getName() + " :Key is not well-formed");
            continue;
          }
          channel->setKey(key);
          applied.push_back(ModeChange(true, 'k', key));
        } else {
          channel->removeKey();

          std::string oldKey;
          size_t stillNeeded = ChannelModes::mandatoryParams(modeStr, i + 1, adding);
          if (paramIdx < msg.params.size() && msg.params.size() - paramIdx > stillNeeded)
            oldKey = msg.params[paramIdx++];

          applied.push_back(ModeChange(false, 'k', oldKey));
        }
        break;
      }
      case 'o': {
        if (paramIdx >= msg.params.size()) {
          if (!alreadyReported(reported, "461")) replyNeedMoreParams(client, "MODE");
          continue;
        }
        std::string nick = msg.params[paramIdx++];
        Client* target = channel->findMember(nick);
        if (!target) {
          if (!alreadyReported(reported, "441:" + ircToLower(nick)))
            sendReply(client, ERR_USERNOTINCHANNEL, nick + " " + channel->getName() + " :They aren't on that channel");
          continue;
        }
        channel->setOperator(target, adding);
        applied.push_back(ModeChange(adding, 'o', target->getNickname()));
        break;
      }
      case 'l': {
        if (adding) {
          if (paramIdx >= msg.params.size()) {
            if (!alreadyReported(reported, "461")) replyNeedMoreParams(client, "MODE");
            continue;
          }
          std::string limitStr = msg.params[paramIdx++];

          long limit = 0;
          if (!libcpp::str::parse_long(limitStr, 1, MAX_USERLIMIT, limit)) {
            if (!alreadyReported(reported, "696:" + limitStr))
              sendReply(client, ERR_INVALIDMODEPARAM,
                        channel->getName() + " l " + limitStr + " :Invalid channel limit");
            continue;
          }
          channel->setUserLimit(static_cast<size_t>(limit));
          applied.push_back(ModeChange(true, 'l', libcpp::str::to_string(limit)));
        } else {
          channel->removeUserLimit();
          applied.push_back(ModeChange(false, 'l'));
        }
        break;
      }
      default: {
        std::string s(1, c);
        if (!alreadyReported(reported, "472:" + s))
          sendReply(client, ERR_UNKNOWNMODE, s + " :is unknown mode char to me");
        break;
      }
    }
  }

  if (!applied.empty()) broadcastModeChanges(channel, client->getPrefix(), applied);
}
