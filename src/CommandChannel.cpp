#include <map>
#include <string>
#include <vector>

#include "IrcMessage.hpp"
#include "IrcName.hpp"
#include "Limits.hpp"
#include "Server.hpp"
#include "ext/IServerExtension.hpp"
#include "libcpp/str/format.hpp"

void Server::cmdJoin(Client* client, const Message& msg) {
  /* "JOIN 0" parts every channel (3.2.1). It is the grammar's own alternative
  ** to a chanlist and captures nothing, so a matched line says so by having no
  ** chanlist -- and that is also why this is tested before the empty-parameter
  ** check below, which it would otherwise trip. On the fallback path it is the
  ** sole parameter being exactly "0". */
  const bool partEverything = msg.matched() ? !msg.has("chanlist") : (msg.params.size() == 1 && msg.params[0] == "0");
  if (partEverything) {
    partAllChannels(client);
    return;
  }

  std::vector<std::string> channels = msg.listOr("chanlist", 0, ',');
  std::vector<std::string> keys = msg.listKeepEmptyOr("keylist", 1, ',');

  /* Named no channel at all: a bare JOIN, an empty name ("JOIN :"), or a list
  ** that is nothing but separators. An empty name is a missing name, and a
  ** client waiting on an answer must not simply hang. */
  if (channels.empty()) {
    replyNeedMoreParams(client, "JOIN");
    return;
  }

  for (size_t i = 0; i < channels.size(); ++i) {
    const std::string& name = channels[i];
    const std::string key = (i < keys.size()) ? keys[i] : "";

    Channel* chan = admitToChannel(client, name, key);
    if (chan == NULL) continue;  //< already a member, or admitToChannel answered

    chan->broadcastMessage(IrcMessage::relay(client->getPrefix(), "JOIN", chan->getName()), NULL);
    for (size_t k = 0; k < _extensions.size(); ++k) _extensions[k]->onJoin(*this, *client, *chan);

    sendJoinBurst(client, chan, name);
  }
}

/*
** NULL means "do not go on": the client is already a member, or a numeric has
** been sent explaining why it may not join. Only a non-NULL return has
** actually added the client to the channel.
*/
Channel* Server::admitToChannel(Client* client, const std::string& name, const std::string& key) {
  if (!IrcName::isChannelName(name)) {  //< per-channel · "JOIN #ok,bad" joins #ok and answers 476 for bad
    sendReply(client, ERR_BADCHANMASK, name);
    return NULL;
  }

  Channel* chan = findChannel(name);
  if (chan == NULL) {
    chan = createChannel(name, client);
    if (chan == NULL) sendNumeric(client, ERR_NOSUCHCHANNEL, name + " :Cannot create channel (server error)");
    return chan;
  }

  if (chan->isMember(client)) return NULL;

  if (chan->isInviteOnly() && !chan->isInvited(client)) {  //< +i without an INVITE -> 473
    sendReply(client, ERR_INVITEONLYCHAN, name);
    return NULL;
  }

  if (!chan->getKey().empty() && (!IrcName::isChannelKey(key) || chan->getKey() != key)) {
    sendReply(client, ERR_BADCHANNELKEY, name);
    return NULL;
  }

  if (chan->getUserLimit() > 0 && chan->getMemberCount() >= chan->getUserLimit()) {  //< +l full -> 471
    sendReply(client, ERR_CHANNELISFULL, name);
    return NULL;
  }

  chan->addMember(client);
  chan->removeInvite(client);
  return chan;
}

/*
** What a client is told about a channel the moment it joins: the topic, the
** member list, and the modes. `name` is the channel as the client typed it --
** the numerics echo that, while the JOIN broadcast above uses the canonical
** stored name, which is what BroadcastsUseCanonicalChannelAndNickCasing pins.
*/
void Server::sendJoinBurst(Client* client, Channel* chan, const std::string& name) {
  if (!chan->getTopic().empty()) {
    sendNumeric(client, RPL_TOPIC, name + " :" + chan->getTopic());
    sendReply(client, RPL_TOPICWHOTIME, name, chan->getTopicSetter(), libcpp::str::to_string(chan->getTopicTime()));
  } else {
    sendReply(client, RPL_NOTOPIC, name);
  }

  const std::string namesHead = "= " + name + " :";
  const size_t framing = 1 + _serverName.size() + 1 + 3 + 1 + client->getNickname().size() + 1 + namesHead.size();
  const size_t budget = (framing + 1 < Limits::kMsgLen - 2) ? Limits::kMsgLen - 2 - framing : 1;
  const std::vector<std::string> chunks = chan->getNamesChunks(budget);
  for (size_t c = 0; c < chunks.size(); ++c) sendNumeric(client, RPL_NAMREPLY, namesHead + chunks[c]);
  sendReply(client, RPL_ENDOFNAMES, name);

  const std::string modeParams = chan->getModeParams();
  std::string modeReply = name + " " + chan->getModeString();
  if (!modeParams.empty()) modeReply += " " + modeParams;
  sendNumeric(client, RPL_CHANNELMODEIS, modeReply);
  sendReply(client, RPL_CREATIONTIME, name, libcpp::str::to_string(chan->getCreationTime()));
}

void Server::cmdPart(Client* client, const Message& msg) {
  const std::string reason = msg.fieldOr("partmsg", 1);
  std::vector<std::string> targets = msg.listOr("chanlist", 0, ',');

  if (targets.empty()) {
    replyNeedMoreParams(client, "PART");
    return;
  }

  for (size_t t = 0; t < targets.size(); ++t) {
    const std::string& chanName = targets[t];
    Channel* chan = requireChannel(client, chanName);
    if (!chan) continue;
    if (!requireMember(client, chan, chanName)) continue;

    const std::string partMsg = reason.empty()
                                    ? IrcMessage::relay(client->getPrefix(), "PART", chan->getName())
                                    : IrcMessage::relay(client->getPrefix(), "PART", chan->getName(), reason);

    chan->broadcastMessage(partMsg, NULL);
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
    for (size_t k = 0; k < _extensions.size(); ++k) _extensions[k]->onPart(*this, *client, *chan);
    chan->removeMember(client);

    if (chan->isEmpty()) removeChannel(names[i]);
  }
}
