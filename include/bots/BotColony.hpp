#ifndef BOTS_BOTCOLONY_HPP
#define BOTS_BOTCOLONY_HPP

#include <string>
#include <vector>

#include "bots/ABot.hpp"
#include "ext/IServerExtension.hpp"

/*
** BotColony — one extension that owns the resident bots.
**
** WHY A COLONY RATHER THAN EIGHT EXTENSIONS
** -----------------------------------------
** Every ABot IS-A IServerExtension, so each could be registered with the
** server directly. Registering them through one owner buys three things:
**
**   1. ONE place decides which bots exist, so the roster is data rather than
**      eight scattered addExtension() calls across two tier files.
**   2. Lifetime is unambiguous. The colony owns the bots and deletes them;
**      the server owns the colony. Nothing is half-owned.
**   3. onPrivmsg() has a well-defined meaning. IServerExtension::onPrivmsg
**      returns bool for "I consumed this" -- and eight independent listeners
**      each returning that answer for the same line is a race waiting to be
**      written. The colony fans out to every bot and always returns false,
**      because a resident bot OBSERVES conversation, it never swallows it.
**
** The colony is deliberately dumb: it holds no cognition of its own, keeps no
** state about the network, and makes no decisions. All of that belongs to the
** individual Brains.
*/
namespace Bots {

class BotColony : public IServerExtension, public IBotAudience {
 public:
  explicit BotColony(Server* server);
  ~BotColony();

  const char* name() const;

  //< Populate from the built-in roster. Split from the constructor so a test
  //< can build an empty colony and add exactly the bots it wants.
  void populate();

  //< Takes ownership.
  void add(ABot* bot);
  std::size_t size() const { return _bots.size(); }
  ABot* at(std::size_t i) { return _bots[i]; }

  // -- IServerExtension: fan out to every resident ------------------------
  void onServerStart(Server& server);
  void onTick(Server& server, std::time_t now);
  void onJoin(Server& server, Client& client, Channel& channel);
  void onPart(Server& server, Client& client, Channel& channel);
  bool onPrivmsg(Server& server, Client& sender, const std::string& target, const std::string& text);
  bool reservesNick(const std::string& nick) const;

  // -- IBotAudience: one resident spoke; let the others hear it -----------
  void botSpoke(ABot* from, const std::string& target, const std::string& text);

 private:
  BotColony(const BotColony& other);
  BotColony& operator=(const BotColony& other);

  Server* _server;
  std::vector<ABot*> _bots;
  std::time_t _lastTick;
  //< Depth guard. A replies to B, B replies to A, and without a bound the
  //< fan-out recurses until the stack gives out. One level is enough for a
  //< bot to answer a bot; beyond that the reply lands on the next tick like
  //< any other conversation, which is also how a human would experience it.
  int _depth;
};

}  // namespace Bots

#endif
