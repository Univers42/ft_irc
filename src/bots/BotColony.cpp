#include "bots/BotColony.hpp"

#include <string>

#include "Channel.hpp"
#include "Client.hpp"
#include "IrcCase.hpp"
#include "Log.hpp"
#include "Server.hpp"
#include "bots/Personalities.hpp"

namespace Bots {

namespace {

/*
** The resident cast.
**
** `role` here is what the bot is willing to ATTEMPT, not what it may do. A
** resident bot is an extension rather than a Client, so the server has never
** granted any of them '@' and none of them can kick -- see ABot::sanction.
** The role still matters: it decides whether a bot escalates at all, and how
** it phrases the escalation.
*/
struct Resident {
  const char* nick;
  const char* personality;
  const char* role;
};

const Resident kResidents[] = {
    {"JokerBot", "joker", "user"},      {"GrumpyBot", "grumpy", "operator"}, {"HappyBot", "happy", "moderator"},
    {"HypeBot", "overexcited", "user"}, {"SadBot", "sad", "user"},           {"FileBot", "file", "filebot"},
    {"OpBot", "operator", "operator"},  {"CalmBot", "calm", "user"},         {NULL, NULL, NULL},
};

}  // namespace

BotColony::BotColony(Server* server) : _server(server), _lastTick(0) {}

BotColony::~BotColony() {
  for (std::vector<ABot*>::size_type i = 0; i < _bots.size(); ++i) delete _bots[i];
}

const char* BotColony::name() const { return "botcolony"; }

void BotColony::add(ABot* bot) {
  if (bot) _bots.push_back(bot);
}

void BotColony::populate() {
  for (int i = 0; kResidents[i].nick; ++i)
    add(make(kResidents[i].personality, _server, kResidents[i].nick, kResidents[i].role));
}

void BotColony::onServerStart(Server& server) {
  (void)server;
  Log::info() << "bot colony online: " << _bots.size() << " residents";
}

void BotColony::onTick(Server& server, std::time_t now) {
  //< The extension tick fires on the server's own schedule. Bots pace
  //< themselves off their cooldowns, but a floor here keeps a fast tick from
  //< turning into a fast channel.
  if (now - _lastTick < 2) return;
  _lastTick = now;
  for (std::vector<ABot*>::size_type i = 0; i < _bots.size(); ++i) _bots[i]->onTick(server, now);
}

void BotColony::onJoin(Server& server, Client& client, Channel& channel) {
  for (std::vector<ABot*>::size_type i = 0; i < _bots.size(); ++i) _bots[i]->onJoin(server, client, channel);
}

void BotColony::onPart(Server& server, Client& client, Channel& channel) {
  for (std::vector<ABot*>::size_type i = 0; i < _bots.size(); ++i) _bots[i]->onPart(server, client, channel);
}

bool BotColony::onPrivmsg(Server& server, Client& sender, const std::string& target, const std::string& text) {
  for (std::vector<ABot*>::size_type i = 0; i < _bots.size(); ++i) _bots[i]->onPrivmsg(server, sender, target, text);
  //< ALWAYS false. A resident bot observes the conversation; it does not
  //< consume it. Returning true would stop the message reaching the humans
  //< it was addressed to.
  return false;
}

bool BotColony::reservesNick(const std::string& nick) const {
  for (std::vector<ABot*>::size_type i = 0; i < _bots.size(); ++i)
    if (_bots[i]->reservesNick(nick)) return true;
  return false;
}

}  // namespace Bots
