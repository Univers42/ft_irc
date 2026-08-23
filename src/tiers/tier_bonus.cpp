#include <new>

#include "Bot.hpp"
#include "Log.hpp"
#include "Server.hpp"
#include "bonus/FileTransferExt.hpp"
#include "ext/RegisterExtensions.hpp"

void configureSettings() {}

void registerExtensions(Server& server) {
  try {
    server.addExtension(new Bot(&server));
    server.addExtension(new FileTransferExt());
  } catch (const std::bad_alloc&) {
    Log::warn(kBonusExtensionsFailed);
  }
}
