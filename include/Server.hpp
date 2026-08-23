#ifndef SERVER_HPP
#define SERVER_HPP

#include <sys/epoll.h>
#include <map>
#include <string>
#include <vector>

#include "Channel.hpp"
#include "Client.hpp"
#include "Dispatch.hpp"
#include "Message.hpp"
#include "Replies.hpp"
#include "grammar/Grammar.hpp"
#include "grammar/IMatcher.hpp"
#include "grammar/MatchResult.hpp"
#include "libcpp98/reactor.hpp"

class IServerExtension;

class Server {
 public:
  Server(int port, const std::string& password, time_t pendingCloseTimeoutSec = PENDING_CLOSE_TIMEOUT);
  ~Server();

  void run();

  Client* findClientByNick(const std::string& nickname) const;
  bool isNickInUse(const std::string& nickname, const Client* except) const;
  Client* findClientByFd(int fd) const;
  void sendToClient(Client* client, const std::string& msg);
  void disconnectClient(int fd, const std::string& reason);

  Channel* findChannel(const std::string& name) const;
  Channel* createChannel(const std::string& name, Client* creator);
  void removeChannel(const std::string& name);

  const std::string& getServerName() const;

  void sendReply(Client* client, const std::string& numeric, const std::string& params);

  void audit(const std::string& event, const std::string& actor, const std::string& detail);

  void addExtension(IServerExtension* ext);

  bool registerExternalFd(int fd, uint32_t events);
  void unregisterExternalFd(int fd);

  static bool isRunning;

 private:
  Server();
  Server(const Server& other);
  Server& operator=(const Server& other);

  void createListenSocket();
  void createEpoll();
  void addToEpoll(int fd, uint32_t events);
  void modifyEpoll(int fd, uint32_t events);
  void removeFromEpoll(int fd);

  void acceptClient();
  void handleClientInput(int fd);
  void handleClientOutput(int fd);
  void handleMessage(Client* client, const std::string& raw);
  void checkTimeouts();
  void checkPendingCloseTimeouts();
  void updateEpollInterest(Client* client);

  void teardownClientState(Client* client, const std::string& reason);

  void disconnectClientNow(int fd, const std::string& reason);

  void finalizeDisconnect(int fd);

  typedef void (Server::*CommandHandler)(Client* client, const Message& msg);

  struct CommandEntry {
    const char* name;
    CommandHandler handler;
    bool needsRegistration;
  };

  static const CommandEntry kCommands[];
  static const CommandEntry* findCommand(const std::string& name);

  void dispatchCommand(Client* client, const Message& msg);
  void partAllChannels(Client* client);

  void initGrammar();
  void bindCommandRules();
  void verifyCommandTable(const std::string& origin) const;
  int commandRule(const std::string& command) const;
  bool parseLine(const std::string& raw, Message& out) const;
  std::string firstToken(const std::string& raw) const;
  void fillParams(const Abnf::MatchResult& fields, Message& out) const;

  void replyNeedMoreParams(Client* client, const std::string& command);
  Channel* requireChannel(Client* client, const std::string& name);
  bool requireMember(Client* client, Channel* channel, const std::string& name);
  bool requireChanOp(Client* client, Channel* channel, const std::string& name);

  void cmdCap(Client* client, const Message& msg);
  void cmdPass(Client* client, const Message& msg);
  void cmdNick(Client* client, const Message& msg);
  void cmdUser(Client* client, const Message& msg);
  void completeRegistration(Client* client);

  void cmdJoin(Client* client, const Message& msg);
  void cmdPart(Client* client, const Message& msg);
  void cmdKick(Client* client, const Message& msg);
  void cmdInvite(Client* client, const Message& msg);
  void cmdTopic(Client* client, const Message& msg);
  void cmdMode(Client* client, const Message& msg);

  void cmdPrivmsg(Client* client, const Message& msg);
  void cmdNotice(Client* client, const Message& msg);
  void cmdPing(Client* client, const Message& msg);
  void cmdPong(Client* client, const Message& msg);
  void cmdQuit(Client* client, const Message& msg);

  void handleUserMode(Client* client, const Message& msg);
  void handleChannelMode(Client* client, Channel* channel, const Message& msg);

  void cmdWho(Client* client, const Message& msg);
  void cmdWhois(Client* client, const Message& msg);
  void cmdUserhost(Client* client, const Message& msg);

  bool isValidNickname(const std::string& nick) const;
  bool isValidChannelName(const std::string& name) const;
  bool isValidChannelKey(const std::string& key) const;
  void broadcastToChannels(Client* client, const std::string& msg);

  int _port;
  std::string _password;
  std::string _serverName;
  int _listenFd;

  libcpp98::Reactor _reactor;
  std::map<int, Client*> _clients;
  std::map<int, uint32_t> _epollMask;
  std::map<std::string, Channel*> _channels;
  std::vector<IServerExtension*> _extensions;
  time_t _lastPingCheck;
  Abnf::Grammar _grammar;
  Abnf::IMatcher* _matcher;
  int _messageRule;
  std::map<std::string, int> _commandRules;
  time_t _pendingCloseTimeoutSec;

  static const int MAX_EVENTS = 64;
};

#endif
