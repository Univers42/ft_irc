#include "Server.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "IrcCase.hpp"
#include "IrcTrace.hpp"
#include "Log.hpp"
#include "ext/IServerExtension.hpp"
#include "grammar/EmbeddedGrammarSource.hpp"
#include "grammar/FileGrammarSource.hpp"
#include "grammar/GrammarBuilder.hpp"
#include "grammar/compiled/ProgramMatcher.hpp"
#include "grammar/interpreted/TreeMatcher.hpp"
#include "libcpp/str/format.hpp"

bool Server::isRunning = true;

Server::Server(int port, const std::string& password, time_t pendingCloseTimeoutSec)
    : _port(port),
      _password(password),
      _serverName(SERVER_NAME),
      _listenFd(-1),
      _reactor(),
      _lastPingCheck(std::time(NULL)),
      _matcher(NULL),
      _messageRule(Abnf::Grammar::kNoRule),
      _pendingCloseTimeoutSec(pendingCloseTimeoutSec) {
  initGrammar();
  createListenSocket();
  createEpoll();
  addToEpoll(_listenFd, EPOLLIN);
}

void Server::audit(const std::string& event, const std::string& actor, const std::string& detail) {
  for (size_t i = 0; i < _extensions.size(); ++i) _extensions[i]->onAudit(event, actor, detail);
}
void Server::addExtension(IServerExtension* ext) {
  if (ext) _extensions.push_back(ext);
}

bool Server::registerExternalFd(int fd, uint32_t events) {
  try {
    addToEpoll(fd, events);
  } catch (const std::exception&) {
    return false;
  }
  return true;
}

void Server::unregisterExternalFd(int fd) { removeFromEpoll(fd); }

void Server::initGrammar() {
  const char* grammarPath = std::getenv("FT_IRC_GRAMMAR");

  Abnf::EmbeddedGrammarSource embedded;
  Abnf::FileGrammarSource file(grammarPath ? grammarPath : "");
  const Abnf::IGrammarSource& source =
      grammarPath ? static_cast<const Abnf::IGrammarSource&>(file) : static_cast<const Abnf::IGrammarSource&>(embedded);

  std::string text;
  if (!source.read(text)) throw std::runtime_error(std::string("cannot read grammar from ") + source.origin());

  Abnf::GrammarBuilder builder;
  if (!builder.compile(text, _grammar))
    throw std::runtime_error(std::string("grammar from ") + source.origin() + ": " + builder.error());

  const char* strategy = std::getenv("FT_IRC_MATCHER");
  if (strategy != NULL && std::string(strategy) == "compiled") {
    Abnf::Compiled::ProgramMatcher* compiled = new Abnf::Compiled::ProgramMatcher(_grammar);
    if (!compiled->compileAll()) {
      const std::string why = compiled->error();
      delete compiled;
      throw std::runtime_error("compiled matcher: " + why);
    }
    _matcher = compiled;
  } else {
    _matcher = new Abnf::Interpreted::TreeMatcher(_grammar);
  }
  _messageRule = _grammar.ruleIndex("message");
  if (_messageRule == Abnf::Grammar::kNoRule) throw std::runtime_error("grammar defines no 'message' rule");

  bindCommandRules();
  verifyCommandTable(source.origin());
  verifyReplyTable();

  Log::debug() << "grammar: " << source.origin() << " -> " << _matcher->strategy() << ", " << _grammar << ", "
               << _commandRules.size() << " command productions";
}

void Server::bindCommandRules() {
  _commandRules.clear();

  const std::string suffix = "-cmd";
  for (std::size_t i = 0; i < _grammar.ruleCount(); ++i) {
    const std::string& rule = _grammar.ruleName(static_cast<int>(i));
    if (rule.size() <= suffix.size()) continue;
    if (rule.compare(rule.size() - suffix.size(), suffix.size(), suffix) != 0) continue;

    std::string name = rule.substr(0, rule.size() - suffix.size());
    for (std::size_t k = 0; k < name.size(); ++k) {
      const char c = name[k];
      if (c >= 'a' && c <= 'z') name[k] = static_cast<char>(c - 'a' + 'A');
    }
    _commandRules[name] = static_cast<int>(i);
  }
}

void Server::verifyReplyTable() const {
  const ReplyText::Entry* rows = ReplyText::table();
  for (const ReplyText::Entry* entry = rows; entry->code != NULL; ++entry) {
    if (std::string(entry->code).size() != 3)
      throw std::runtime_error("reply table: " + std::string(entry->code) + " is not a three-digit numeric");
    if (entry->text != NULL && *entry->text == '\0')
      throw std::runtime_error("reply table: " + std::string(entry->code) + " has empty text");
    for (const ReplyText::Entry* other = rows; other != entry; ++other)
      if (std::string(other->code) == entry->code)
        throw std::runtime_error("reply table: duplicate numeric " + std::string(entry->code));
  }
}

void Server::verifyCommandTable(const std::string& origin) const {
  std::vector<std::string> missingHandler;
  for (std::map<std::string, int>::const_iterator it = _commandRules.begin(); it != _commandRules.end(); ++it)
    if (findCommand(it->first) == NULL) missingHandler.push_back(it->first);

  std::vector<std::string> missingRule;
  for (const CommandEntry* entry = kCommands; entry->name != NULL; ++entry)
    if (_commandRules.find(entry->name) == _commandRules.end()) missingRule.push_back(entry->name);

  if (missingHandler.empty() && missingRule.empty()) return;

  std::string why = "grammar from " + origin + " does not match the command table:";
  for (size_t i = 0; i < missingHandler.size(); ++i) why += " " + missingHandler[i] + "-cmd has no handler;";
  for (size_t i = 0; i < missingRule.size(); ++i) why += " " + missingRule[i] + " has no grammar rule;";
  throw std::runtime_error(why);
}

int Server::commandRule(const std::string& command) const {
  std::map<std::string, int>::const_iterator it = _commandRules.find(command);
  if (it == _commandRules.end()) return Abnf::Grammar::kNoRule;
  return it->second;
}

std::string Server::firstToken(const std::string& raw) const {
  std::size_t i = 0;
  while (i < raw.size() && raw[i] == ' ') ++i;
  if (i < raw.size() && raw[i] == ':') return std::string();

  std::string name;
  while (i < raw.size() && raw[i] != ' ') {
    const char c = raw[i++];
    name += (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
  }
  return name;
}

void Server::fillParams(const Abnf::MatchResult& fields, Message& out) const {
  const int commandSlot = _grammar.captureIndex("command");
  const int prefixSlot = _grammar.captureIndex("prefix");

  out.params.clear();
  for (std::size_t i = 0; i < fields.sequenceSize(); ++i) {
    const int owner = fields.sequenceOwner(i);
    if (owner == commandSlot || owner == prefixSlot) continue;
    out.params.push_back(fields.sequenceAt(i));
  }
}

bool Server::parseLine(const std::string& raw, Message& out) const {
  Abnf::MatchResult result;
  if (!_matcher->match(_messageRule, raw, result)) return false;

  out.command = result.get("command");
  for (std::size_t i = 0; i < out.command.size(); ++i) {
    const char c = out.command[i];
    if (c >= 'a' && c <= 'z') out.command[i] = static_cast<char>(c - 'a' + 'A');
  }

  fillParams(result, out);
  if (result.count("trail") > 0) out.trailingIndex = static_cast<int>(out.params.size()) - 1;
  return true;
}

Server::~Server() {
  delete _matcher;
  _matcher = NULL;

  for (std::vector<IServerExtension*>::reverse_iterator it = _extensions.rbegin(); it != _extensions.rend(); ++it)
    delete *it;
  for (std::map<int, Client*>::iterator it = _clients.begin(); it != _clients.end(); ++it) {
    close(it->first);
    delete it->second;
  }
  for (std::map<std::string, Channel*>::iterator it = _channels.begin(); it != _channels.end(); ++it) {
    delete it->second;
  }

  if (_listenFd >= 0) close(_listenFd);
}

void Server::createListenSocket() {
  _listenFd = socket(AF_INET, SOCK_STREAM, 0);
  if (_listenFd < 0) throw std::runtime_error("socket() failed: " + std::string(strerror(errno)));

  int opt = 1;
  if (setsockopt(_listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    throw std::runtime_error("setsockopt() failed: " + std::string(strerror(errno)));

  if (fcntl(_listenFd, F_SETFL, O_NONBLOCK) < 0)
    throw std::runtime_error("fcntl() failed: " + std::string(strerror(errno)));

  struct sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(_port);

  if (bind(_listenFd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0)
    throw std::runtime_error("bind() failed: " + std::string(strerror(errno)));

  if (listen(_listenFd, SOMAXCONN) < 0) throw std::runtime_error("listen() failed: " + std::string(strerror(errno)));
}

void Server::createEpoll() {
  if (!_reactor.open()) throw std::runtime_error("epoll_create1() failed: " + std::string(strerror(errno)));
}

void Server::addToEpoll(int fd, uint32_t events) {
  if (!_reactor.add(fd, events)) throw std::runtime_error("epoll_ctl ADD failed: " + std::string(strerror(errno)));
}

void Server::modifyEpoll(int fd, uint32_t events) {
  if (!_reactor.modify(fd, events)) Log::error() << "epoll_ctl MOD failed: " << strerror(errno);
}

void Server::removeFromEpoll(int fd) {
  if (!_reactor.remove(fd) && errno != ENOENT && errno != EBADF)
    Log::error() << "epoll_ctl DEL failed: " << strerror(errno);
}

void Server::run() {
  struct epoll_event events[MAX_EVENTS];

  Log::banner() << "ft_irc - listening on port " << _port;

  for (size_t i = 0; i < _extensions.size(); ++i) _extensions[i]->onServerStart(*this);

  while (isRunning) {
    int nfds = epoll_wait(_reactor.fd(), events, MAX_EVENTS, 1000);
    if (nfds < 0) {
      if (errno == EINTR) continue;
      throw std::runtime_error("epoll_wait() failed: " + std::string(strerror(errno)));
    }

    for (int i = 0; i < nfds; ++i) {
      int fd = events[i].data.fd;
      uint32_t ev = events[i].events;

      if (fd == _listenFd) {
        acceptClient();
      } else if (_clients.count(fd)) {
        if (ev & EPOLLIN) handleClientInput(fd);
        if ((ev & EPOLLOUT) && _clients.count(fd)) handleClientOutput(fd);
        if ((ev & (EPOLLERR | EPOLLHUP)) && _clients.count(fd)) disconnectClientNow(fd, "Connection error");
      } else {
        removeFromEpoll(fd);
      }
    }

    checkTimeouts();
    checkPendingCloseTimeouts();

    time_t now = std::time(NULL);
    for (size_t i = 0; i < _extensions.size(); ++i) _extensions[i]->onTick(*this, now);

    for (std::map<int, Client*>::iterator it = _clients.begin(); it != _clients.end(); ++it)
      updateEpollInterest(it->second);
  }
}

void Server::acceptClient() {
  struct sockaddr_in clientAddr;
  socklen_t addrLen = sizeof(clientAddr);

  int clientFd = accept(_listenFd, reinterpret_cast<struct sockaddr*>(&clientAddr), &addrLen);
  if (clientFd < 0) return;

  if (_clients.size() >= MAX_CLIENTS) {
    close(clientFd);
    Log::warn("connection rejected: MAX_CLIENTS reached");
    return;
  }

  if (fcntl(clientFd, F_SETFL, O_NONBLOCK) < 0) {
    Log::error() << "fcntl() failed on client fd: " << strerror(errno);
    close(clientFd);
    return;
  }

  std::string hostname = inet_ntoa(clientAddr.sin_addr);
  Client* client;
  try {
    client = new Client(clientFd, hostname);
  } catch (const std::bad_alloc&) {
    Log::error("out of memory: cannot accept new client");
    close(clientFd);
    return;
  }
  _clients[clientFd] = client;

  addToEpoll(clientFd, EPOLLIN);
  _epollMask[clientFd] = EPOLLIN;

  IrcTrace::sessionOpen(clientFd, hostname);
  Log::info() << "new connection from " << hostname << " (fd " << clientFd << ")";
}

void Server::handleClientInput(int fd) {
  if (_clients.find(fd) == _clients.end()) return;

  Client* client = _clients[fd];
  char buf[MAX_MSGLEN + 1];

  ssize_t bytesRead = recv(fd, buf, MAX_MSGLEN, 0);
  if (bytesRead <= 0) {
    if (bytesRead == 0) disconnectClient(fd, "Connection closed");
    return;
  }

  buf[bytesRead] = '\0';
  client->appendToRecvBuffer(std::string(buf, bytesRead));
  client->updateLastActivity();
  client->setPingSent(false);

  std::vector<std::string> messages = client->extractMessages();
  for (size_t i = 0; i < messages.size(); ++i) {
    handleMessage(client, messages[i]);

    std::map<int, Client*>::iterator cit = _clients.find(fd);
    if (cit == _clients.end() || cit->second->isPendingClose()) return;
  }

  if (client->isSendQExceeded()) disconnectClientNow(fd, "SendQ exceeded");
}

void Server::handleClientOutput(int fd) {
  std::map<int, Client*>::iterator it = _clients.find(fd);
  if (it == _clients.end()) return;

  Client* client = it->second;
  if (!client->hasPendingData()) return;

  const std::string& buf = client->getSendBuffer();
  ssize_t bytesSent = send(fd, buf.c_str(), buf.size(), 0);
  if (bytesSent < 0) return;
  client->clearSendBuffer(bytesSent);

  if (client->isPendingClose()) {
    if (!client->hasPendingData() || client->isSendQExceeded()) finalizeDisconnect(fd);
    return;
  }

  if (client->isSendQExceeded()) disconnectClientNow(fd, "SendQ exceeded");
}

void Server::handleMessage(Client* client, const std::string& raw) {
  IrcTrace::inbound(client->getFd(), client->getNickname(), raw);

  Message msg;
  Abnf::MatchResult fields;

  const std::string name = firstToken(raw);
  const int rule = name.empty() ? Abnf::Grammar::kNoRule : commandRule(name);

  if (rule != Abnf::Grammar::kNoRule && _matcher->match(rule, raw, fields)) {
    msg.command = name;
    msg.fields = &fields;
    fillParams(fields, msg);
  } else if (!parseLine(raw, msg)) {
    return;
  }

  if (msg.command.empty()) return;

  Log::trace() << "parsed: " << msg;
  dispatchCommand(client, msg);
}

void Server::updateEpollInterest(Client* client) {
  int fd = client->getFd();

  uint32_t want = (client->isPendingClose() ? 0u : EPOLLIN) | (client->hasPendingData() ? EPOLLOUT : 0u);
  std::map<int, uint32_t>::iterator it = _epollMask.find(fd);
  if (it != _epollMask.end() && it->second == want) return;
  modifyEpoll(fd, want);
  _epollMask[fd] = want;
}

void Server::checkTimeouts() {
  time_t now = std::time(NULL);
  if (now - _lastPingCheck < 30) return;
  _lastPingCheck = now;

  std::vector<int> sendQNow;
  std::vector<int> pingTimeoutDeferred;
  for (std::map<int, Client*>::iterator it = _clients.begin(); it != _clients.end(); ++it) {
    Client* client = it->second;

    if (client->isPendingClose()) continue;
    time_t idle = now - client->getLastActivity();

    if (client->isSendQExceeded()) {
      sendQNow.push_back(it->first);
    } else if (client->isPingSent() && idle > PING_INTERVAL + PING_TIMEOUT) {
      pingTimeoutDeferred.push_back(it->first);
    } else if (!client->isPingSent() && idle > PING_INTERVAL) {
      sendToClient(client, "PING :" + _serverName);
      client->setPingSent(true);
    }
  }

  for (size_t i = 0; i < sendQNow.size(); ++i) disconnectClientNow(sendQNow[i], "SendQ exceeded");
  for (size_t i = 0; i < pingTimeoutDeferred.size(); ++i) disconnectClient(pingTimeoutDeferred[i], "Ping timeout");
}

void Server::checkPendingCloseTimeouts() {
  time_t now = std::time(NULL);
  std::vector<int> expired;
  for (std::map<int, Client*>::iterator it = _clients.begin(); it != _clients.end(); ++it) {
    if (it->second->isPendingClose() && now - it->second->getPendingCloseSince() >= _pendingCloseTimeoutSec)
      expired.push_back(it->first);
  }
  for (size_t i = 0; i < expired.size(); ++i) {
    struct linger lg;
    lg.l_onoff = 1;
    lg.l_linger = 0;
    setsockopt(expired[i], SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
    finalizeDisconnect(expired[i]);
  }
}

const Server::CommandEntry Server::kCommands[] = {
    {"CAP", &Server::cmdCap, false},
    {"PASS", &Server::cmdPass, false},
    {"NICK", &Server::cmdNick, false},
    {"USER", &Server::cmdUser, false},
    {"QUIT", &Server::cmdQuit, false},
    {"PONG", &Server::cmdPong, false},
    {"PING", &Server::cmdPing, true},
    {"JOIN", &Server::cmdJoin, true},
    {"PART", &Server::cmdPart, true},
    {"PRIVMSG", &Server::cmdPrivmsg, true},
    {"NOTICE", &Server::cmdNotice, true},
    {"KICK", &Server::cmdKick, true},
    {"INVITE", &Server::cmdInvite, true},
    {"TOPIC", &Server::cmdTopic, true},
    {"MODE", &Server::cmdMode, true},
    {"WHO", &Server::cmdWho, true},
    {"WHOIS", &Server::cmdWhois, true},
    {"USERHOST", &Server::cmdUserhost, true},
    {NULL, NULL, false},
};

const Server::CommandEntry* Server::findCommand(const std::string& name) { return Dispatch::find(kCommands, name); }

void Server::replyNeedMoreParams(Client* client, const std::string& command) {
  sendReply(client, ERR_NEEDMOREPARAMS, command);
}

Channel* Server::requireChannel(Client* client, const std::string& name) {
  Channel* channel = findChannel(name);
  if (channel == NULL) sendReply(client, ERR_NOSUCHCHANNEL, name);
  return channel;
}

bool Server::requireMember(Client* client, Channel* channel, const std::string& name) {
  if (channel->isMember(client)) return true;
  sendReply(client, ERR_NOTONCHANNEL, name);
  return false;
}

bool Server::requireChanOp(Client* client, Channel* channel, const std::string& name) {
  if (channel->isOperator(client)) return true;
  sendReply(client, ERR_CHANOPRIVSNEEDED, name);
  return false;
}

void Server::dispatchCommand(Client* client, const Message& msg) {
  const CommandEntry* entry = findCommand(msg.command);

  if (entry != NULL && !entry->needsRegistration) {
    (this->*entry->handler)(client, msg);
    return;
  }

  if (!client->isRegistered()) {
    sendReply(client, ERR_NOTREGISTERED);
    return;
  }

  if (entry != NULL) {
    (this->*entry->handler)(client, msg);
    return;
  }

  for (size_t i = 0; i < _extensions.size(); ++i) {
    if (_extensions[i]->onCommand(*this, *client, msg)) return;
  }

  sendReply(client, ERR_UNKNOWNCOMMAND, msg.command);
}

Client* Server::findClientByFd(int fd) const {
  std::map<int, Client*>::const_iterator it = _clients.find(fd);
  return it == _clients.end() ? NULL : it->second;
}

Client* Server::findClientByNick(const std::string& nickname) const {
  for (std::map<int, Client*>::const_iterator it = _clients.begin(); it != _clients.end(); ++it) {
    if (!it->second->isRegistered() || it->second->isTearingDown()) continue;
    if (ircEquals(it->second->getNickname(), nickname)) return it->second;
  }
  return NULL;
}

bool Server::isNickInUse(const std::string& nickname, const Client* except) const {
  for (std::map<int, Client*>::const_iterator it = _clients.begin(); it != _clients.end(); ++it) {
    if (it->second == except || it->second->isTearingDown()) continue;
    if (ircEquals(it->second->getNickname(), nickname)) return true;
  }
  return false;
}

void Server::sendToClient(Client* client, const std::string& msg) {
  client->queueMessage(":" + _serverName + " " + msg);
}

static std::string replyBody(const std::string& numeric, const std::string& params) {
  const char* text = ReplyText::find(numeric);
  if (text == NULL) return params;
  if (params.empty()) return ":" + std::string(text);
  return params + " :" + text;
}

void Server::sendNumeric(Client* client, const std::string& numeric, const std::string& params) {
  client->queueMessage(":" + _serverName + " " + numeric + " " + client->getNickname() + " " + params);
}

void Server::sendReply(Client* client, const std::string& numeric) {
  sendNumeric(client, numeric, replyBody(numeric, ""));
}

void Server::sendReply(Client* client, const std::string& numeric, const std::string& p0) {
  sendNumeric(client, numeric, replyBody(numeric, p0));
}

void Server::sendReply(Client* client, const std::string& numeric, const std::string& p0, const std::string& p1) {
  sendNumeric(client, numeric, replyBody(numeric, p0 + " " + p1));
}

void Server::sendReply(Client* client, const std::string& numeric, const std::string& p0, const std::string& p1,
                       const std::string& p2) {
  sendNumeric(client, numeric, replyBody(numeric, p0 + " " + p1 + " " + p2));
}

void Server::teardownClientState(Client* client, const std::string& reason) {
  if (client->isTearingDown()) return;
  client->markTearingDown();

  int fd = client->getFd();
  std::string prefix = client->getPrefix();
  std::string quitMsg = ":" + prefix + " QUIT :" + reason;

  std::set<int> alreadySent;
  alreadySent.insert(fd);
  for (std::map<std::string, Channel*>::iterator it = _channels.begin(); it != _channels.end();) {
    Channel* chan = it->second;

    chan->removeInvite(client);
    if (chan->isMember(client)) {
      std::vector<Client*> members = chan->getMembers();
      for (size_t i = 0; i < members.size(); ++i) {
        int mfd = members[i]->getFd();
        if (alreadySent.count(mfd)) continue;
        members[i]->queueMessage(quitMsg);
        alreadySent.insert(mfd);
      }
      chan->removeMember(client);
      if (chan->isEmpty()) {
        delete chan;
        _channels.erase(it++);
        continue;
      }
    }
    ++it;
  }

  for (size_t i = 0; i < _extensions.size(); ++i) _extensions[i]->onClientDisconnect(*this, *client, reason);

  IrcTrace::sessionClose(fd, client->getNickname(), reason);
  Log::info() << "client disconnected: " << *client << " (" << reason << ")";
  audit("disconnect", client->getNickname(), reason);
}

void Server::finalizeDisconnect(int fd) {
  _epollMask.erase(fd);
  removeFromEpoll(fd);
  close(fd);
  delete _clients[fd];
  _clients.erase(fd);
}

void Server::disconnectClient(int fd, const std::string& reason) {
  std::map<int, Client*>::iterator cit = _clients.find(fd);
  if (cit == _clients.end()) return;

  Client* client = cit->second;
  if (client->isTearingDown()) return;

  teardownClientState(client, reason);
  if (!client->hasPendingData()) {
    finalizeDisconnect(fd);
    return;
  }
  client->markPendingClose();
}

void Server::disconnectClientNow(int fd, const std::string& reason) {
  std::map<int, Client*>::iterator cit = _clients.find(fd);
  if (cit == _clients.end()) return;

  Client* client = cit->second;
  if (client->isPendingClose()) {
    finalizeDisconnect(fd);
    return;
  }
  if (client->isTearingDown()) return;

  teardownClientState(client, reason);
  finalizeDisconnect(fd);
}

Channel* Server::findChannel(const std::string& name) const {
  std::map<std::string, Channel*>::const_iterator it = _channels.find(ircToLower(name));
  if (it != _channels.end()) return it->second;
  return NULL;
}

Channel* Server::createChannel(const std::string& name, Client* creator) {
  Channel* channel;
  try {
    channel = new Channel(name, creator);
  } catch (const std::bad_alloc&) {
    Log::error() << "out of memory: cannot create channel " << name;
    return NULL;
  }
  _channels[ircToLower(name)] = channel;
  return channel;
}

void Server::removeChannel(const std::string& name) {
  std::map<std::string, Channel*>::iterator it = _channels.find(ircToLower(name));
  if (it != _channels.end()) {
    delete it->second;
    _channels.erase(it);
  }
}

const std::string& Server::getServerName() const { return _serverName; }

static bool isAsciiAlpha(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

static bool isAsciiDigit(char c) { return c >= '0' && c <= '9'; }

static bool isNickSpecial(char c) {
  const unsigned char u = static_cast<unsigned char>(c);
  return (u >= 0x5B && u <= 0x60) || (u >= 0x7B && u <= 0x7D);
}

static bool isNickLead(char c) { return isAsciiAlpha(c) || isNickSpecial(c); }

static bool isNickBody(char c) { return isAsciiAlpha(c) || isAsciiDigit(c) || isNickSpecial(c) || c == '-'; }

bool Server::isValidNickname(const std::string& nick) const {
  if (nick.empty()) return false;

  if (!isNickLead(nick[0])) return false;
  for (size_t i = 1; i < nick.size(); ++i)
    if (!isNickBody(nick[i])) return false;
  return true;
}

bool Server::isValidChannelName(const std::string& name) const {
  if (name.empty() || name.size() > MAX_CHANNELLEN) return false;
  if (name[0] != '#') return false;
  if (name.size() < 2) return false;

  for (size_t i = 0; i < name.size(); ++i) {
    if (name[i] == ' ' || name[i] == '\x07' || name[i] == ',') return false;
    if (name[i] == ':') return false;
  }
  return true;
}

bool Server::isValidChannelKey(const std::string& key) const {
  if (key.empty() || key.size() > MAX_KEYLEN) return false;
  for (std::string::size_type i = 0; i < key.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(key[i]);
    if (c <= ' ' || c == ',') return false;
    if (c > 0x7F) return false;
  }
  return true;
}

void Server::broadcastToChannels(Client* client, const std::string& msg) {
  std::set<int> alreadySent;
  alreadySent.insert(client->getFd());

  for (std::map<std::string, Channel*>::iterator it = _channels.begin(); it != _channels.end(); ++it) {
    Channel* chan = it->second;
    if (!chan->isMember(client)) continue;
    std::vector<Client*> members = chan->getMembers();
    for (size_t i = 0; i < members.size(); ++i) {
      if (alreadySent.count(members[i]->getFd())) continue;
      members[i]->queueMessage(msg);
      alreadySent.insert(members[i]->getFd());
    }
  }
}
