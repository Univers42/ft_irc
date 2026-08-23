#include <map>
#include <string>
#include <vector>

#include "Server.hpp"
#include "ext/IServerExtension.hpp"
#include "libcpp/str/format.hpp"

void Server::cmdJoin(Client* client, const Message& msg) {
  if (!msg.matched()) {
    replyNeedMoreParams(client, "JOIN");
    return;
  }

  if (!msg.has("chanlist")) {
    partAllChannels(client);
    return;
  }

  std::vector<std::string> channels = msg.list("chanlist", ',');
  std::vector<std::string> keys = msg.listKeepEmpty("keylist", ',');

  for (size_t i = 0; i < channels.size(); ++i) {
    const std::string& name = channels[i];
    std::string key = (i < keys.size()) ? keys[i] : "";

    if (!isValidChannelName(name)) {
      sendReply(client, ERR_BADCHANMASK, name + " :Bad Channel Mask");
      continue;
    }

    Channel* chan = findChannel(name);
    if (chan) {
      if (chan->isMember(client)) continue;

      if (chan->isInviteOnly() && !chan->isInvited(client)) {
        sendReply(client, ERR_INVITEONLYCHAN, name + " :Cannot join channel (+i)");
        continue;
      }

      if (!chan->getKey().empty() &&
          (!isValidChannelKey(key) || chan->getKey() != key)) {
        sendReply(client, ERR_BADCHANNELKEY, name + " :Cannot join channel (+k)");
        continue;
      }

      if (chan->getUserLimit() > 0 && chan->getMemberCount() >= chan->getUserLimit()) {
        sendReply(client, ERR_CHANNELISFULL, name + " :Cannot join channel (+l)");
        continue;
      }

      chan->addMember(client);
      chan->removeInvite(client);
    } else {
      chan = createChannel(name, client);
      if (!chan) {
        sendReply(client, ERR_NOSUCHCHANNEL, name + " :Cannot create channel (server error)");
        continue;
      }
    }

    chan->broadcastMessage(":" + client->getPrefix() + " JOIN " + chan->getName(), NULL);
    audit("join", client->getNickname(), name);
    for (size_t k = 0; k < _extensions.size(); ++k) _extensions[k]->onJoin(*this, *client, *chan);

    if (!chan->getTopic().empty()) {
      sendReply(client, RPL_TOPIC, name + " :" + chan->getTopic());
      sendReply(client, RPL_TOPICWHOTIME,
                name + " " + chan->getTopicSetter() + " " + libcpp::str::to_string(chan->getTopicTime()));
    } else {
      sendReply(client, RPL_NOTOPIC, name + " :No topic is set");
    }

    std::string namesHead = "= " + name + " :";
    size_t framing = 1 + _serverName.size() + 1 + 3 + 1 + client->getNickname().size() + 1 + namesHead.size();
    size_t budget =
        (framing + 1 < static_cast<size_t>(MAX_MSGLEN) - 2) ? static_cast<size_t>(MAX_MSGLEN) - 2 - framing : 1;
    std::vector<std::string> chunks = chan->getNamesChunks(budget);
    for (size_t c = 0; c < chunks.size(); ++c) sendReply(client, RPL_NAMREPLY, namesHead + chunks[c]);
    sendReply(client, RPL_ENDOFNAMES, name + " :End of /NAMES list");

    std::string modes = chan->getModeString();
    std::string modeParams = chan->getModeParams();
    std::string modeReply = name + " " + modes;
    if (!modeParams.empty()) modeReply += " " + modeParams;
    sendReply(client, RPL_CHANNELMODEIS, modeReply);
    sendReply(client, RPL_CREATIONTIME, name + " " + libcpp::str::to_string(chan->getCreationTime()));
  }
}

void Server::cmdPart(Client* client, const Message& msg) {
  if (!msg.matched()) {
    replyNeedMoreParams(client, "PART");
    return;
  }

  const std::string reason = msg.field("partmsg");

  std::vector<std::string> targets = msg.list("chanlist", ',');

  for (size_t t = 0; t < targets.size(); ++t) {
    const std::string& chanName = targets[t];
    Channel* chan = requireChannel(client, chanName);
    if (!chan) continue;
    if (!requireMember(client, chan, chanName)) continue;

    std::string partMsg = ":" + client->getPrefix() + " PART " + chan->getName();
    if (!reason.empty()) partMsg += " :" + reason;

    chan->broadcastMessage(partMsg, NULL);
    audit("part", client->getNickname(), chanName);
    for (size_t k = 0; k < _extensions.size(); ++k) _extensions[k]->onPart(*this, *client, *chan);
    chan->removeMember(client);

    if (chan->isEmpty()) removeChannel(chanName);
  }
}

void Server::partAllChannels(Client* client) {
  std::vector<std::string> names;
  for (std::map<std::string, Channel*>::iterator it = _channels.begin(); it != _channels.end(); ++it)
    if (it->second->isMember(client)) names.push_back(it->first);

  for (size_t i = 0; i < names.size(); ++i) {
    Channel* chan = findChannel(names[i]);
    if (chan == NULL) continue;

    const std::string partMsg = ":" + client->getPrefix() + " PART " + chan->getName();

    chan->broadcastMessage(partMsg, NULL);
    audit("part", client->getNickname(), names[i]);
    for (size_t k = 0; k < _extensions.size(); ++k) _extensions[k]->onPart(*this, *client, *chan);
    chan->removeMember(client);

    if (chan->isEmpty()) removeChannel(names[i]);
  }
}
