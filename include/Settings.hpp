#ifndef SETTINGS_HPP
#define SETTINGS_HPP

#include <cstddef>
#include <ctime>
#include <ostream>
#include <string>

#include "Limits.hpp"

class Settings {
 public:
  Settings()
      : serverName(Limits::kServerName),
        serverVersion(Limits::kServerVersion),
        serverCreated(Limits::kServerCreated),
        sendQ(Limits::kSendQ),
        maxClients(Limits::kMaxClients),
        pingInterval(Limits::kPingInterval),
        pingTimeout(Limits::kPingTimeout),
        pendingCloseTimeout(Limits::kPendingCloseTimeout),
        pingSweepInterval(Limits::kPingSweepInterval) {}

  Settings(const Settings& other)
      : serverName(other.serverName),
        serverVersion(other.serverVersion),
        serverCreated(other.serverCreated),
        sendQ(other.sendQ),
        maxClients(other.maxClients),
        pingInterval(other.pingInterval),
        pingTimeout(other.pingTimeout),
        pendingCloseTimeout(other.pendingCloseTimeout),
        pingSweepInterval(other.pingSweepInterval) {}

  Settings& operator=(const Settings& other) {
    if (this == &other) return *this;
    serverName = other.serverName;
    serverVersion = other.serverVersion;
    serverCreated = other.serverCreated;
    sendQ = other.sendQ;
    maxClients = other.maxClients;
    pingInterval = other.pingInterval;
    pingTimeout = other.pingTimeout;
    pendingCloseTimeout = other.pendingCloseTimeout;
    pingSweepInterval = other.pingSweepInterval;
    return *this;
  }

  ~Settings() {}

  std::string serverName;
  std::string serverVersion;
  std::string serverCreated;

  std::size_t sendQ;
  std::size_t maxClients;

  time_t pingInterval;
  time_t pingTimeout;
  time_t pendingCloseTimeout;
  time_t pingSweepInterval;
};

inline std::ostream& operator<<(std::ostream& os, const Settings& s) {
  return os << s.serverName << " " << s.serverVersion << " (" << s.serverCreated << "), sendq " << s.sendQ << " B, max "
            << s.maxClients << " clients, ping " << s.pingInterval << "/" << s.pingTimeout << " s every "
            << s.pingSweepInterval << " s, pending close " << s.pendingCloseTimeout << " s";
}

inline Settings& settings() {
  static Settings instance;
  return instance;
}

#endif
