#ifndef ISERVEREXTENSION_HPP
#define ISERVEREXTENSION_HPP

#include <stdint.h>
#include <ctime>
#include <string>

class Server;
class Client;
class Channel;
struct Message;

class IServerExtension {
 public:
  virtual ~IServerExtension() {}

  virtual const char* name() const = 0;

  virtual void onServerStart(Server& server) { (void)server; }
  virtual void onTick(Server& server, time_t now) {
    (void)server;
    (void)now;
  }

  virtual void onClientRegistered(Server& server, Client& client) {
    (void)server;
    (void)client;
  }

  virtual void onClientDisconnect(Server& server, Client& client,
                                  const std::string& reason) {
    (void)server;
    (void)client;
    (void)reason;
  }

  virtual void onJoin(Server& server, Client& client, Channel& channel) {
    (void)server;
    (void)client;
    (void)channel;
  }
  virtual void onPart(Server& server, Client& client, Channel& channel) {
    (void)server;
    (void)client;
    (void)channel;
  }

  virtual bool onCommand(Server& server, Client& client, const Message& msg) {
    (void)server;
    (void)client;
    (void)msg;
    return false;
  }
  virtual bool onPrivmsg(Server& server, Client& sender,
                         const std::string& target, const std::string& text) {
    (void)server;
    (void)sender;
    (void)target;
    (void)text;
    return false;
  }
  virtual bool reservesNick(const std::string& nick) const {
    (void)nick;
    return false;
  }

  virtual bool ownsFd(int fd) const {
    (void)fd;
    return false;
  }
  virtual void onFdEvent(Server& server, int fd, uint32_t events) {
    (void)server;
    (void)fd;
    (void)events;
  }

  virtual void onAudit(const std::string& event, const std::string& actor,
                       const std::string& detail) {
    (void)event;
    (void)actor;
    (void)detail;
  }
};

#endif
