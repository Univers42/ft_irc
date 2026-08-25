#include <new>

#include "Bot.hpp"
#include "Log.hpp"
#include "Server.hpp"
#include "bonus/FileTransferExt.hpp"
#include "bots/BotColony.hpp"
#include "ext/RegisterExtensions.hpp"

void configureSettings() {}

void registerExtensions(Server& server) {
  try {
    server.addExtension(new Bot(&server));
    server.addExtension(new FileTransferExt());

    //< The resident bot colony. Constructed then populated so a bad_alloc
    //< halfway through the cast still leaves a colony the server can own and
    //< destroy -- populate() adds what it managed to build.
    Bots::BotColony* colony = new Bots::BotColony(&server);
    colony->populate();
    server.addExtension(colony);
  } catch (const std::bad_alloc&) {
    Log::warn(kBonusExtensionsFailed);
  }
}
