#include "Bot.hpp"

#include <ctime>
#include <string>

#include "Client.hpp"
#include "IrcCase.hpp"
#include "Limits.hpp"
#include "Server.hpp"
#include "Settings.hpp"
#include "libcpp/data/date.hpp"
#include "libcpp/str/format.hpp"

const char* Bot::_jokes[] = {"Why do programmers prefer dark mode? Because light attracts bugs.",
                             "There are only 10 types of people in the world: those who understand "
                             "binary and those who don't.",
                             "A SQL query walks into a bar, sees two tables and asks: 'Can I JOIN you?'",
                             "Why do Java programmers wear glasses? Because they don't C#.",
                             "!false — It's funny because it's true.",
                             "How many programmers does it take to change a light bulb? None, that's a "
                             "hardware problem.",
                             "An IRC user walks into a bar. The bartender says: 'What'll it be?' The "
                             "user says: '/quit'.",
                             "Knock knock. Who's there? Recursion. Recursion who? Knock knock."};

const int Bot::_jokeCount = 8;

const Bot::BotCommand Bot::kBotCommands[] = {
    {"!help", &Bot::cmdHelp},
    {"!time", &Bot::cmdTime},
    {"!info", &Bot::cmdInfo},
    {"!joke", &Bot::cmdJoke},
    {NULL, NULL},
};

Bot::Bot(Server* server) : _server(server), _nickname("ircbot"), _nextJoke(0) {}
Bot::~Bot() {}

const char* Bot::name() const { return "bot"; }

bool Bot::onPrivmsg(Server& server, Client& sender, const std::string& target, const std::string& text) {
  (void)server;
  if (!ircEquals(target, _nickname)) return false;
  handleMessage(&sender, text);
  return true;
}

bool Bot::reservesNick(const std::string& nick) const { return ircEquals(nick, _nickname); }

void Bot::handleMessage(Client* sender, const std::string& text) {
  if (text.empty()) return;

  std::string cmd;
  std::string param;

  std::istringstream iss(text);
  iss >> cmd;
  if (iss) std::getline(iss >> std::ws, param);

  const BotCommand* entry = Dispatch::find(kBotCommands, cmd);
  if (entry == NULL) {
    reply(sender, "Unknown command. Type !help for available commands.");
    return;
  }

  (this->*entry->handler)(sender, param);
}

void Bot::cmdHelp(Client* sender, const std::string& param) {
  (void)param;
  reply(sender, "Available commands:");
  reply(sender, "  !help           - Show this help message");
  reply(sender, "  !time           - Show current server time");
  reply(sender, "  !info [#chan]    - Show server or channel info");
  reply(sender, "  !joke           - Tell a random joke");
}

void Bot::cmdTime(Client* sender, const std::string& param) {
  (void)param;
  reply(sender, "Server time: " + libcpp::data::format_now("%Y-%m-%d %H:%M:%S"));
}

void Bot::cmdInfo(Client* sender, const std::string& param) {
  if (param.empty() || param[0] != '#') {
    reply(sender, "Server: " + _server->getServerName() + " v" + settings().serverVersion);
    return;
  }

  Channel* chan = _server->findChannel(param);
  if (!chan) {
    reply(sender, "Channel " + param + " does not exist.");
    return;
  }

  reply(sender, "Channel " + param + ": " + libcpp::str::to_string(chan->getMemberCount()) +
                    " users, modes: " + chan->getModeString());

  if (!chan->getTopic().empty()) reply(sender, "Topic: " + chan->getTopic());
}

void Bot::cmdJoke(Client* sender, const std::string& param) {
  (void)param;
  reply(sender, _jokes[_nextJoke]);
  _nextJoke = (_nextJoke + 1) % _jokeCount;
}

void Bot::reply(Client* sender, const std::string& text) {
  sender->queueMessage(":" + _nickname + " PRIVMSG " + sender->getNickname() + " :" + text);
}
