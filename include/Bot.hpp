#ifndef BOT_HPP
#define BOT_HPP

#include <string>
#include <vector>

#include "Dispatch.hpp"
#include "ext/IServerExtension.hpp"

class Server;
class Client;
struct Message;

class Bot : public IServerExtension {
 public:
  explicit Bot(Server* server);
  ~Bot();

  const char* name() const;
  bool onPrivmsg(Server& server, Client& sender, const std::string& target, const std::string& text);
  bool reservesNick(const std::string& nick) const;

  void handleMessage(Client* sender, const std::string& text);

 private:
  Bot();
  Bot(const Bot& other);
  Bot& operator=(const Bot& other);

  typedef void (Bot::*CommandHandler)(Client* sender, const std::string& param);
  typedef Dispatch::Entry<CommandHandler> BotCommand;

  static const BotCommand kBotCommands[];

  void cmdHelp(Client* sender, const std::string& param);
  void cmdTime(Client* sender, const std::string& param);
  void cmdInfo(Client* sender, const std::string& param);
  void cmdJoke(Client* sender, const std::string& param);

  void reply(Client* sender, const std::string& text);

  Server* _server;
  std::string _nickname;

  int _nextJoke;

  static const char* _jokes[];
  static const int _jokeCount;
};

#endif
