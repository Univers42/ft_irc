#include <cstddef>
#include <cstdlib>
#include <ctime>
#include <new>
#include <string>

#include "AuditLog.hpp"
#include "Bot.hpp"
#include "Log.hpp"
#include "Server.hpp"
#include "Settings.hpp"
#include "bonus/FileTransferExt.hpp"
#include "ext/RegisterExtensions.hpp"
#include "extras/FancyLogSink.hpp"
#include "libcpp/util/config.hpp"

namespace {

std::size_t sizeSetting(const libcpp::util::Config& cfg, const char* key, std::size_t fallback) {
  const int value = cfg.get_int("limits", key, -1);
  if (value <= 0) return fallback;
  return static_cast<std::size_t>(value);
}

time_t timeSetting(const libcpp::util::Config& cfg, const char* key, time_t fallback) {
  const int value = cfg.get_int("limits", key, -1);
  if (value <= 0) return fallback;
  return static_cast<time_t>(value);
}

}  // namespace

void configureSettings() {
  const char* cfgPath = std::getenv("FT_IRC_CONFIG");
  if (!cfgPath) return;

  libcpp::util::Config cfg;
  if (!cfg.load_file(cfgPath)) return;

  Settings& s = settings();
  s.serverName = cfg.get("server", "name", s.serverName);
  s.serverVersion = cfg.get("server", "version", s.serverVersion);
  s.serverCreated = cfg.get("server", "created", s.serverCreated);

  s.sendQ = sizeSetting(cfg, "sendq", s.sendQ);
  s.maxClients = sizeSetting(cfg, "max_clients", s.maxClients);
  s.pingInterval = timeSetting(cfg, "ping_interval", s.pingInterval);
  s.pingTimeout = timeSetting(cfg, "ping_timeout", s.pingTimeout);
  s.pingSweepInterval = timeSetting(cfg, "ping_sweep_interval", s.pingSweepInterval);
  s.pendingCloseTimeout = timeSetting(cfg, "pending_close_timeout", s.pendingCloseTimeout);
}

static void registerPlatformFeatures(Server& server) {
  const char* cfgPath = std::getenv("FT_IRC_CONFIG");
  if (!cfgPath) return;

  libcpp::util::Config cfg;
  if (!cfg.load_file(cfgPath)) {
    Log::warn(std::string("could not read FT_IRC_CONFIG: ") + cfgPath);
    return;
  }

  if (cfg.get_bool("audit", "enabled", false)) {
    std::string path = cfg.get("audit", "path", "./ircserv-audit.csv");
    AuditLog* audit = NULL;
    try {
      audit = new AuditLog(path);
    } catch (const std::bad_alloc&) {
      audit = NULL;
    }
    if (audit && !audit->ok()) {
      Log::warn("could not open audit log: " + path);
      delete audit;
    } else if (audit) {
      server.addExtension(audit);
      Log::info("audit log: " + path);
    }
  }
}

void registerExtensions(Server& server) {
  try {
    Log::setSink(new FancyLogSink());
  } catch (const std::bad_alloc&) {
  }

  try {
    server.addExtension(new Bot(&server));
    server.addExtension(new FileTransferExt());
  } catch (const std::bad_alloc&) {
    Log::warn("could not create bonus extensions (out of memory)");
  }

  registerPlatformFeatures(server);
}
