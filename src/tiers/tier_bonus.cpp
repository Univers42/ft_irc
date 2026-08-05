/* ─── Tier: bonus ───
**
** Mandatory kernel + the subject's bonus features: the bot and file
** transfer. Built by `make bonus`.
*/

#include "Bot.hpp"
#include "Log.hpp"
#include "Server.hpp"
#include "bonus/FileTransferExt.hpp"
#include "ext/RegisterExtensions.hpp"

#include <new>

void registerExtensions(Server& server) {
  try {
    server.addExtension(new Bot(&server));
    server.addExtension(new FileTransferExt());
  } catch (const std::bad_alloc&) {
    Log::warn("could not create bonus extensions (out of memory)");
  }
}
