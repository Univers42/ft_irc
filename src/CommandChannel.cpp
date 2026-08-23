#include <map>
#include <string>
#include <vector>

#include "IrcMessage.hpp"
#include "Limits.hpp"
#include "Server.hpp"
#include "ext/IServerExtension.hpp"
#include "libcpp/str/format.hpp"

void Server::cmdJoin(Client* client, const Message& msg) {
  if (!msg.matched()) {
    replyNeedMoreParams(client, "JOIN");
    return;
  }

  if (!msg.has("chanlist")) {  //< "JOIN 0" · the grammar's own alternative · parts every channel (3.2.1)
    partAllChannels(client);
    return;
  }

  std::vector<std::string> channels = msg.list("chanlist", ',');
  std::vector<std::string> keys = msg.listKeepEmpty("keylist", ',');

  for (size_t i = 0; i < channels.size(); ++i) {
    const std::string& name = channels[i];
    std::string key = (i < keys.size()) ? keys[i] : "";

    if (!isValidChannelName(name)) {  //< per-channel · "JOIN #ok,bad" joins #ok and answers 476 for bad
      sendReply(client, ERR_BADCHANMASK, name);
      continue;
    }

    Channel* chan = findChannel(name);
    if (chan) {
      if (chan->isMember(client)) continue;

      if (chan->isInviteOnly() && !chan->isInvited(client)) {  //< +i without an INVITE -> 473
        sendReply(client, ERR_INVITEONLYCHAN, name);
        continue;
      }

      if (!chan->getKey().empty() && (!isValidChannelKey(key) || chan->getKey() != key)) {
        sendReply(client, ERR_BADCHANNELKEY, name);
        continue;
      }

      if (chan->getUserLimit() > 0 && chan->getMemberCount() >= chan->getUserLimit()) {  //< +l full -> 471
        sendReply(client, ERR_CHANNELISFULL, name);
        continue;
      }

      chan->addMember(client);
      chan->removeInvite(client);
    } else {
      chan = createChannel(name, client);
      if (!chan) {
        sendNumeric(client, ERR_NOSUCHCHANNEL, name + " :Cannot create channel (server error)");
        continue;
      }
    }

    chan->broadcastMessage(IrcMessage::relay(client->getPrefix(), "JOIN", chan->getName()), NULL);
    audit("join", client->getNickname(), name);
    for (size_t k = 0; k < _extensions.size(); ++k) _extensions[k]->onJoin(*this, *client, *chan);

    if (!chan->getTopic().empty()) {
      sendNumeric(client, RPL_TOPIC, name + " :" + chan->getTopic());
      sendReply(client, RPL_TOPICWHOTIME, name, chan->getTopicSetter(), libcpp::str::to_string(chan->getTopicTime()));
    } else {
      sendReply(client, RPL_NOTOPIC, name);
    }

    std::string namesHead = "= " + name + " :";
    size_t framing = 1 + _serverName.size() + 1 + 3 + 1 + client->getNickname().size() + 1 + namesHead.size();
    size_t budget = (framing + 1 < Limits::kMsgLen - 2) ? Limits::kMsgLen - 2 - framing : 1;
    std::vector<std::string> chunks = chan->getNamesChunks(budget);
    for (size_t c = 0; c < chunks.size(); ++c) sendNumeric(client, RPL_NAMREPLY, namesHead + chunks[c]);
    sendReply(client, RPL_ENDOFNAMES, name);

    std::string modes = chan->getModeString();
    std::string modeParams = chan->getModeParams();
    std::string modeReply = name + " " + modes;
    if (!modeParams.empty()) modeReply += " " + modeParams;
    sendNumeric(client, RPL_CHANNELMODEIS, modeReply);
    sendReply(client, RPL_CREATIONTIME, name, libcpp::str::to_string(chan->getCreationTime()));
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

    const std::string partMsg = reason.empty()
                                    ? IrcMessage::relay(client->getPrefix(), "PART", chan->getName())
                                    : IrcMessage::relay(client->getPrefix(), "PART", chan->getName(), reason);

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

    const std::string partMsg = IrcMessage::relay(client->getPrefix(), "PART", chan->getName());

    chan->broadcastMessage(partMsg, NULL);
    audit("part", client->getNickname(), names[i]);
    for (size_t k = 0; k < _extensions.size(); ++k) _extensions[k]->onPart(*this, *client, *chan);
    chan->removeMember(client);

    if (chan->isEmpty()) removeChannel(names[i]);
  }
}
