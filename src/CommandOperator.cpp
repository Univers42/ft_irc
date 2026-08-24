#include <stdexcept>
#include <string>
#include <vector>

#include "ChannelModes.hpp"
#include "IrcCase.hpp"
#include "IrcMessage.hpp"
#include "IrcName.hpp"
#include "Limits.hpp"
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
  const size_t budget = Limits::kMsgLen - 2;

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
  const std::string head = IrcMessage::relay(prefix, "MODE", channel->getName()) + " ";
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
    sendReply(client, ERR_USERNOTINCHANNEL, target, chanName);
    return;
  }

  std::string kickMsg =
      IrcMessage::relay(client->getPrefix(), "KICK", chan->getName() + " " + targetClient->getNickname(), reason);
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
    sendReply(client, ERR_NOSUCHNICK, target);
    return;
  }

  if (chan->isMember(targetClient)) {
    sendReply(client, ERR_USERONCHANNEL, target, chanName);
    return;
  }

  chan->addInvite(targetClient);

  sendReply(client, RPL_INVITING, target, chanName);

  targetClient->queueMessage(IrcMessage::relay(client->getPrefix(), "INVITE", target, chanName));
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
      sendReply(client, RPL_NOTOPIC, chanName);
    } else {
      sendNumeric(client, RPL_TOPIC, chanName + " :" + chan->getTopic());
      sendReply(client, RPL_TOPICWHOTIME, chanName, chan->getTopicSetter(),
                libcpp::str::to_string(chan->getTopicTime()));
    }
    return;
  }

  if (chan->isTopicRestricted() && !requireChanOp(client, chan, chanName)) return;

  std::string newTopic = msg.fieldOr("topictext", 1);
  if (newTopic.size() > Limits::kTopicLen) newTopic.erase(Limits::kTopicLen);
  chan->setTopic(newTopic, client->getNickname());

  chan->broadcastMessage(IrcMessage::relay(client->getPrefix(), "TOPIC", chan->getName(), newTopic), NULL);
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
      sendNumeric(client, RPL_CHANNELMODEIS, reply);

      sendReply(client, RPL_CREATIONTIME, target, libcpp::str::to_string(chan->getCreationTime()));
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
    sendReply(client, ERR_USERSDONTMATCH);
    return;
  }

  if (msg.params.size() == 1) {
    sendNumeric(client, RPL_UMODEIS, client->getUserModeString());
    return;
  }

  const std::string& modeStr = msg.params[1];

  if (modeStr.empty() || (modeStr[0] != '+' && modeStr[0] != '-')) return;  //< "i" "it" "o bob" -> no sign, no reply

  bool adding = true;
  std::vector<ModeChange> applied;
  std::vector<std::string> reported;

  for (size_t i = 0; i < modeStr.size(); ++i) {
    char c = modeStr[i];

    if (c == '+') {  //< USER mode sign · "MODE bob +i" · "+iw" both · "+o" parsed then IGNORED (RFC 3.1.5)
      adding = true;
      continue;
    }
    if (c == '-') {  //< "-i" clears · "-o" IS honoured (self-deop allowed) · "+i-w" flips mid-string
      adding = false;
      continue;
    }

    if (c == 'i') {  //< invisible · "USER u 8 * :R" sets this too, via the 3.1.3 bitmask
      client->setInvisible(adding);
      applied.push_back(ModeChange(adding, 'i'));
    } else if (c == 'w') {
      client->setWallops(adding);
      applied.push_back(ModeChange(adding, 'w'));
    } else {
      std::string s(1, c);
      if (!alreadyReported(reported, "501:" + s)) sendReply(client, ERR_UMODEUNKNOWNFLAG, s);
    }
  }

  if (!applied.empty()) {
    const std::string head = IrcMessage::relay(client->getPrefix(), "MODE", client->getNickname()) + " ";
    const std::vector<std::string> lines = renderModeLines(head, applied);
    for (size_t i = 0; i < lines.size(); ++i) client->queueMessage(lines[i]);
  }
}

/*
** The state one mode letter is applied against. handleChannelMode() used to
** carry all of this as locals in a 109-line body, which is why the
** "this letter needs a parameter I do not have" check was written out three
** times: there was nowhere shared to put it.
*/
struct Server::ModeApply {
  Client* client;
  Channel* channel;
  const Message* msg;
  const std::string* modeStr;
  std::size_t letterIndex;
  std::size_t paramIdx;
  bool adding;
  std::vector<ModeChange>* applied;
  std::vector<std::string>* reported;
};

/* The one copy of "consume the next parameter, or answer 461 once". Answering
** once rather than once per occurrence is the ModeReplyStorm fix, so it has to
** live on this path rather than at each call site. */
bool Server::takeModeParam(ModeApply& ctx, std::string& out) {
  if (ctx.paramIdx >= ctx.msg->params.size()) {
    if (!alreadyReported(*ctx.reported, "461")) replyNeedMoreParams(ctx.client, "MODE");
    return false;
  }
  out = ctx.msg->params[ctx.paramIdx++];
  return true;
}

void Server::applyModeInviteOnly(ModeApply& ctx) {
  ctx.channel->setInviteOnly(ctx.adding);
  ctx.applied->push_back(ModeChange(ctx.adding, 'i'));
}

void Server::applyModeTopicLock(ModeApply& ctx) {
  ctx.channel->setTopicRestricted(ctx.adding);
  ctx.applied->push_back(ModeChange(ctx.adding, 't'));
}

void Server::applyModeKey(ModeApply& ctx) {
  if (ctx.adding) {
    std::string key;
    if (!takeModeParam(ctx, key)) return;
    if (!IrcName::isChannelKey(key)) {
      if (!alreadyReported(*ctx.reported, "525:" + key)) sendReply(ctx.client, ERR_INVALIDKEY, ctx.channel->getName());
      return;
    }
    ctx.channel->setKey(key);
    ctx.applied->push_back(ModeChange(true, 'k', key));
    return;
  }

  ctx.channel->removeKey();

  /* -k's argument is optional-greedy: it takes the next parameter only if the
  ** letters after it do not need it themselves, so "-k+o bob" gives bob to +o
  ** rather than swallowing it as the old key. */
  std::string oldKey;
  const std::size_t stillNeeded = ChannelModes::mandatoryParams(*ctx.modeStr, ctx.letterIndex + 1, ctx.adding);
  if (ctx.paramIdx < ctx.msg->params.size() && ctx.msg->params.size() - ctx.paramIdx > stillNeeded)
    oldKey = ctx.msg->params[ctx.paramIdx++];

  ctx.applied->push_back(ModeChange(false, 'k', oldKey));
}

void Server::applyModeOperator(ModeApply& ctx) {
  std::string nick;
  if (!takeModeParam(ctx, nick)) return;

  Client* target = ctx.channel->findMember(nick);
  if (!target) {
    if (!alreadyReported(*ctx.reported, "441:" + ircToLower(nick)))
      sendReply(ctx.client, ERR_USERNOTINCHANNEL, nick, ctx.channel->getName());
    return;
  }
  ctx.channel->setOperator(target, ctx.adding);
  ctx.applied->push_back(ModeChange(ctx.adding, 'o', target->getNickname()));
}

void Server::applyModeLimit(ModeApply& ctx) {
  if (!ctx.adding) {
    ctx.channel->removeUserLimit();
    ctx.applied->push_back(ModeChange(false, 'l'));
    return;
  }

  std::string limitStr;
  if (!takeModeParam(ctx, limitStr)) return;

  long limit = 0;
  if (!libcpp::str::parse_long(limitStr, 1, Limits::kUserLimit, limit)) {
    if (!alreadyReported(*ctx.reported, "696:" + limitStr))
      sendReply(ctx.client, ERR_INVALIDMODEPARAM, ctx.channel->getName(), "l", limitStr);
    return;
  }
  ctx.channel->setUserLimit(static_cast<std::size_t>(limit));
  ctx.applied->push_back(ModeChange(true, 'l', libcpp::str::to_string(limit)));
}

struct Server::ChannelModeHandler {
  char letter;
  void (Server::*apply)(ModeApply&);
};

/* The letters live in ChannelModes::table(), which IrcTrace also reads to know
** which parameter holds a key. This table says what each one does. Server's
** verifyChannelModeTable() refuses to start if the two disagree, so a letter
** can never be declared in one and forgotten in the other. */
const Server::ChannelModeHandler Server::kChannelModeHandlers[] = {
    {'i', &Server::applyModeInviteOnly}, {'t', &Server::applyModeTopicLock}, {'k', &Server::applyModeKey},
    {'o', &Server::applyModeOperator},   {'l', &Server::applyModeLimit},     {'\0', 0},
};

const Server::ChannelModeHandler* Server::findChannelModeHandler(char letter) {
  for (const Server::ChannelModeHandler* entry = kChannelModeHandlers; entry->letter != '\0'; ++entry)
    if (entry->letter == letter) return entry;
  return 0;
}

void Server::verifyChannelModeTable() const {
  for (const ChannelModes::Spec* spec = ChannelModes::table(); spec->letter != '\0'; ++spec)
    if (findChannelModeHandler(spec->letter) == 0)
      throw std::runtime_error(std::string("channel mode +") + spec->letter + " has an arity but no handler");

  for (const Server::ChannelModeHandler* entry = kChannelModeHandlers; entry->letter != '\0'; ++entry)
    if (ChannelModes::find(entry->letter) == 0)
      throw std::runtime_error(std::string("channel mode +") + entry->letter + " has a handler but no arity");
}

void Server::handleChannelMode(Client* client, Channel* channel, const Message& msg) {
  if (!requireMember(client, channel, channel->getName())) return;
  if (!requireChanOp(client, channel, channel->getName())) return;

  const std::string& modeStr = msg.params[1];
  if (modeStr.empty() || (modeStr[0] != '+' && modeStr[0] != '-')) return;

  std::vector<ModeChange> applied;
  std::vector<std::string> reported;

  ModeApply ctx;
  ctx.client = client;
  ctx.channel = channel;
  ctx.msg = &msg;
  ctx.modeStr = &modeStr;
  ctx.letterIndex = 0;
  ctx.paramIdx = 2;
  ctx.adding = true;
  ctx.applied = &applied;
  ctx.reported = &reported;

  for (std::size_t i = 0; i < modeStr.size(); ++i) {
    const char c = modeStr[i];

    if (c == '+' || c == '-') {  //< "+it" · "-o+i" flips · "+-+-i" -> only the last sign counts
      ctx.adding = (c == '+');
      continue;
    }

    const ChannelModeHandler* handler = findChannelModeHandler(c);
    if (handler == 0) {
      const std::string s(1, c);
      if (!alreadyReported(reported, "472:" + s)) sendReply(client, ERR_UNKNOWNMODE, s);
      continue;
    }

    ctx.letterIndex = i;
    (this->*handler->apply)(ctx);
  }

  if (!applied.empty()) broadcastModeChanges(channel, client->getPrefix(), applied);
}
