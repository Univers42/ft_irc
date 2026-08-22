#ifndef PLATFORMBUS_HPP
#define PLATFORMBUS_HPP

#include <map>
#include <string>

#include "ext/IServerExtension.hpp"
#include "libcpp98/line_buffer.hpp"

class Server;

class PlatformBus : public IServerExtension {
 public:
  PlatformBus(Server* server, int port, const std::string& secret,
              const std::string& serviceNick);
  ~PlatformBus();

  const char* name() const;
  void onServerStart(Server& server);
  bool ownsFd(int fd) const;
  void onFdEvent(Server& server, int fd, uint32_t events);

  bool start();
  bool owns(int fd) const;
  void acceptConnection();
  void handleInput(int fd);
  void closeConnection(int fd);

 private:
  PlatformBus();
  PlatformBus(const PlatformBus& other);
  PlatformBus& operator=(const PlatformBus& other);

  struct Conn {
    bool authed;
    libcpp98::LineBuffer buffer;

    Conn() : authed(false), buffer() {}
  };

  void handleLine(int fd, const std::string& line);
  void send(int fd, const std::string& text);
  void publish(const std::string& channel, const std::string& type,
               const std::string& message);

  Server* _server;
  int _port;
  std::string _secret;
  std::string _serviceNick;
  int _listenFd;
  std::map<int, Conn> _conns;
};

#endif
