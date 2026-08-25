#include "bots/ABot.hpp"

#include <string>
#include <vector>

#include "Channel.hpp"
#include "Client.hpp"
#include "IrcCase.hpp"
#include "IrcMessage.hpp"
#include "Server.hpp"
#include "bots/Lexicon.hpp"

/*
** The fixed algorithm. Every branch below is the same for all eight bots;
** what differs is which hook it calls and what that hook returns.
*/
namespace Bots {

ABot::ABot(Server* server, const std::string& nick, const std::string& role)
    : _chattiness(0.20f),
      _cooldown(30.0f),
      _replyOdds(0.75f),
      _moderation(0.10f),
      _sensitivity(1.0f),
      _grudge(1.0f),
      _warnAt(3.0f),
      _finalAt(6.0f),
      _kickAt(9.0f),
      _shouts(false),
      _server(server),
      _brain(NULL),
      _nick(nick),
      _role(role),
      _seed(0) {
  //< Seed from the nick so a bot's behaviour is stable across runs but
  //< different from its neighbour's -- one shared RNG would couple every
  //< bot's timing to every other's.
  for (std::string::size_type i = 0; i < nick.size(); ++i) _seed = _seed * 131u + static_cast<unsigned char>(nick[i]);
  if (!_seed) _seed = 1u;
}

ABot::~ABot() { delete _brain; }

void ABot::setBrain(Brain* brain) {
  if (brain == _brain) return;
  delete _brain;
  _brain = brain;
}

const char* ABot::name() const { return "abot"; }

bool ABot::reservesNick(const std::string& nick) const { return ircEquals(nick, _nick); }

float ABot::roll() const {
  //< A 32-bit LCG. C++98 has no <random>, and rand() is a global shared by
  //< every bot -- which would make one bot's decisions perturb another's.
  _seed = _seed * 1103515245u + 12345u;
  return static_cast<float>((_seed >> 16) & 0x7FFFu) / 32767.0f;
}

std::string ABot::pick(const char* const* pool) const {
  if (!pool || !pool[0]) return "";
  int n = 0;
  while (pool[n]) ++n;
  return pool[static_cast<int>(roll() * static_cast<float>(n)) % n];
}

std::string ABot::style(const std::string& text) const {
  std::string out(text);
  if (_shouts && roll() < 0.30f) {
    for (std::string::size_type i = 0; i < out.size(); ++i)
      out[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[i])));
  }
  if (!_excitement.empty() && roll() < 0.55f) out += _excitement;
  return out;
}

void ABot::say(Server& server, const std::string& target, const std::string& text) {
  if (text.empty()) return;
  _brain->noteSpoke(std::time(NULL));
  const std::string line = ":" + _nick + " PRIVMSG " + target + " :" + style(text);

  if (!target.empty() && target[0] == '#') {
    Channel* chan = server.findChannel(target);
    if (chan) chan->broadcastMessage(line, NULL);
    return;
  }
  Client* who = server.findClientByNick(target);
  if (who) who->queueMessage(line);
}

// ── the fixed algorithm ────────────────────────────────────────────────────

bool ABot::onPrivmsg(Server& server, Client& sender, const std::string& target, const std::string& text) {
  if (!_brain) return false;
  if (ircEquals(sender.getNickname(), _nick)) return false;

  const std::time_t now = std::time(NULL);
  const std::string speaker = sender.getNickname();
  const Brain::Reading r = _brain->read(text, speaker, target, now);
  const bool channel = !target.empty() && target[0] == '#';
  const std::string replyTo = channel ? target : speaker;

  // 1. offensive language -> the ladder. Everyone FEELS it; mayReact decides
  //    who actually speaks.
  if (r.impact > 0.0f) {
    _brain->woundedBy(speaker, r.impact, _grudge);
    if (mayReact(target, speaker, r)) escalate(server, target, speaker, r);
    return false;  //< never swallow the message; it is a real PRIVMSG
  }

  // 2. warmth. Gratitude and sympathy move relationships, which then colour
  //    everything this bot says to that person afterwards.
  if (Lexicon::isAppreciation(text) && (r.atMe || r.aboutMe)) {
    _brain->warmedBy(speaker, 1);
    if (!_brain->onCooldown(now, _cooldown / 3.0f) && roll() < 0.45f) {
      say(server, replyTo, onThanked(speaker));
      return false;
    }
  }
  if (Lexicon::isSympathy(text) && (r.atMe || r.aboutMe)) {
    _brain->warmedBy(speaker, 2);
    const std::string reply = onSympathy(speaker);
    if (!reply.empty()) {
      say(server, replyTo, reply);
      return false;
    }
  }

  // 3. spoken to directly — the strongest obligation a bot has
  if (r.atMe) {
    if (_brain->onCooldown(now, _cooldown / 4.0f)) return false;
    if (roll() > _replyOdds) return false;
    say(server, replyTo, flavourFor(speaker, onAddressed(r, speaker, text)));
    return false;
  }

  // 4. talked ABOUT rather than TO. Noticing this is most of what makes a
  //    bot feel present in a room rather than merely responsive.
  if (!r.addressee.empty() && r.addressee == _nick && roll() < 0.5f) {
    if (!_brain->onCooldown(now, _cooldown / 3.0f)) {
      say(server, replyTo, onMentioned(speaker));
      return false;
    }
  }

  // 4. ambient, governed by the thread rather than a bare die roll
  if (!channel || _brain->onCooldown(now, _cooldown / 2.0f)) return false;
  if (roll() > _brain->urgeToSpeak(target, _chattiness, now)) return false;

  //< An unanswered question outranks whatever this bot had to say. A channel
  //< where somebody asks something and everyone carries on with their own
  //< material is the specific way this used to feel fake.
  const Thread* t = _brain->peekThread(target);
  if (t && t->questionOpen(now) && t->questionFrom != _nick) {
    say(server, target, answerLine());
    return false;
  }
  say(server, target, onAmbient(target));
  return false;
}

bool ABot::mayReact(const std::string& channel, const std::string& speaker, const Brain::Reading& r) const {
  //< A bot on a real rung always speaks: escalation is not optional, and a
  //< warning that got drowned out is a warning nobody received.
  if (_brain->rungFor(speaker, _warnAt, _finalAt, _kickAt) >= 2) return true;
  if (r.atMe) return true;  //< said to my face

  const Thread* t = _brain->peekThread(channel);
  if (!t) return true;
  //< Already three answers in six seconds: that is a pile-on, not a channel.
  if (t->rate(std::time(NULL), 6) >= 3) return false;

  //< Sensitivity decides who is most likely to speak up, so an angry room is
  //< answered by the bots that actually care about it.
  float odds = 0.15f + r.impact * 0.35f;
  if (odds > 0.75f) odds = 0.75f;
  return roll() < odds;
}

void ABot::escalate(Server& server, const std::string& channel, const std::string& speaker, const Brain::Reading& r) {
  const int rung = _brain->rungFor(speaker, _warnAt, _finalAt, _kickAt);
  const int prior = _brain->rungUsed(speaker);

  //< Never repeat a rung. Repeating is what made the first version look
  //< stuck, warning the same person forever with the same sentence.
  if (rung <= prior && rung < 4) {
    if (roll() < 0.25f) say(server, channel, onInsult(r, speaker));
    return;
  }
  _brain->climbed(speaker, rung);

  const bool inChannel = !channel.empty() && channel[0] == '#';
  switch (rung) {
    case 1:
      say(server, channel, onInsult(r, speaker));
      return;
    case 2:
      say(server, channel, warningLine(speaker, false));
      return;
    case 3:
      //< An opped bot tightens the room before reaching for a kick, which is
      //< the move a real operator makes first.
      if (inChannel && _brain->isOpIn(channel) && roll() < _moderation + 0.4f) {
        tighten(server, channel);
        return;
      }
      say(server, channel, warningLine(speaker, true));
      return;
    default:
      sanction(server, channel, speaker);
      return;
  }
}

void ABot::tighten(Server& server, const std::string& channel) {
  //< Only i, t, k, l and o exist here (005: CHANMODES=,,kl,it PREFIX=(o)@).
  //< There is no +m and no +b, so "moderate the channel" is not available
  //< and inventing it would be testing our own imagination.
  if (!_brain->hasMode(channel, 't')) {
    _brain->countAttempt();
    _brain->sawMode(channel, 't', true);
    say(server, channel, "locking the topic until this settles");
    return;
  }
  if (!_brain->hasMode(channel, 'i')) {
    _brain->countAttempt();
    _brain->sawMode(channel, 'i', true);
    say(server, channel, "making this invite-only until things calm down");
  }
}

void ABot::sanction(Server& server, const std::string& channel, const std::string& speaker) {
  /*
  ** Rung 4, and the honest one.
  **
  ** An in-server bot is an EXTENSION, not a Client: it has no file
  ** descriptor, it is not in the channel's member list, and the server has
  ** therefore never granted it '@'. It genuinely cannot kick anybody.
  **
  ** The first version of this function faked it -- broadcast a KICK line and
  ** called Channel::removeMember() behind the server's back. That produced
  ** the right-looking output and was wrong in the way that matters: it
  ** granted the bot a privilege the server never gave it, and it bypassed
  ** every permission check in cmdKick(). A bot that can remove users without
  ** being an operator is not testing the server, it is lying about it.
  **
  ** So the ultimate sanction available to a resident bot is to NAME the
  ** problem to the people who can act. That is what a real service bot does,
  ** and it keeps the server the only authority on who may remove whom.
  **
  ** The Python ecosystem's bots are real clients over real sockets; THEY can
  ** be opped and their rung 4 is a genuine KICK that the server accepts or
  ** refuses. The two halves test different things on purpose.
  */
  _brain->countAttempt();

  Channel* chan = server.findChannel(channel);
  if (!chan) return;

  //< Address the operators who are actually present, so the escalation goes
  //< somewhere rather than into the room in general.
  const std::vector<std::string> here = _brain->peers(channel);
  std::string ops;
  for (std::vector<std::string>::size_type i = 0; i < here.size(); ++i) {
    if (!_brain->isOp(channel, here[i])) continue;
    if (!ops.empty()) ops += ", ";
    ops += here[i];
  }

  if (ops.empty()) {
    say(server, channel,
        speaker +
            " has been warned twice and has not stopped. "
            "no operator is here to act on it");
  } else {
    say(server, channel, ops + ": " + speaker + " has ignored two warnings — " + kickReason());
  }
  //< Not a refusal by the server; a limit this bot knows it has.
  _brain->countRefusal();
}

void ABot::onJoin(Server& server, Client& client, Channel& channel) {
  if (!_brain) return;
  const std::string who = client.getNickname();
  _brain->sawJoin(channel.getName(), who);
  if (ircEquals(who, _nick)) return;
  if (roll() > greetOdds()) return;
  if (_brain->onCooldown(std::time(NULL), _cooldown / 2.0f)) return;
  say(server, channel.getName(), greetLine(who));
}

void ABot::onPart(Server& server, Client& client, Channel& channel) {
  (void)server;
  if (_brain) _brain->sawPart(channel.getName(), client.getNickname());
}

void ABot::onTick(Server& server, std::time_t now) {
  if (!_brain) return;
  _brain->settle();

  const std::vector<std::string> chans = _brain->channels();
  if (chans.empty()) return;

  //< Housekeeping first, and it is NOT gated by the chat cooldown: reopening
  //< a channel this bot locked is a duty, not conversation.
  for (std::vector<std::string>::size_type i = 0; i < chans.size(); ++i)
    if (relax(server, chans[i])) return;

  if (idleSpecial(server, now)) return;

  if (_brain->onCooldown(now, _cooldown)) return;
  if (roll() > _chattiness * 0.35f) return;

  const std::string& chan = chans[static_cast<std::size_t>(roll() * static_cast<float>(chans.size())) % chans.size()];
  const Thread* t = _brain->peekThread(chan);
  if (t && t->questionOpen(now) && t->questionFrom != _nick) {
    say(server, chan, answerLine());
    return;
  }
  say(server, chan, idleLine());
}

bool ABot::relax(Server& server, const std::string& channel) {
  //< Only undo what this bot itself believes is set, and only once the thread
  //< has actually cooled -- lifting a lockdown while the argument is still
  //< running just restarts it.
  const Thread* t = _brain->peekThread(channel);
  if (!t || t->heat > 0.12f) return false;
  if (!_brain->hasMode(channel, 'i')) return false;
  _brain->sawMode(channel, 'i', false);
  _brain->countAttempt();
  say(server, channel, "things have settled — opening this back up");
  return true;
}

// ── default hook bodies ────────────────────────────────────────────────────

std::string ABot::warningLine(const std::string& speaker, bool isFinal) const {
  return isFinal ? speaker + ": final warning. next one and you're out" : speaker + ": watch your language";
}

std::string ABot::kickReason() const { return "repeated abuse after warnings"; }

std::string ABot::onAddressed(const Brain::Reading& r, const std::string& speaker, const std::string& text) {
  (void)text;
  if (r.question) return answerLine();
  if (!r.keywords.empty()) return speaker + ": " + r.keywords[0] + ", yeah — i've seen that";
  return speaker + ": go on";
}

std::string ABot::onAmbient(const std::string& channel) {
  const Thread* t = _brain->peekThread(channel);
  if (t && !t->topic.empty() && roll() < 0.5f) return "about " + t->topic[0] + " — i'd check that twice";
  return idleLine();
}

std::string ABot::greetLine(const std::string& who) const { return "hi " + who; }

std::string ABot::answerLine() const { return "not sure, honestly"; }

float ABot::greetOdds() const { return 0.4f; }

std::string ABot::onMentioned(const std::string& speaker) {
  return speaker + ": i can hear you, you know";
}

std::string ABot::onThanked(const std::string& speaker) { return "anytime, " + speaker; }

std::string ABot::onSympathy(const std::string& speaker) {
  (void)speaker;
  return "";  //< most bots do not need looking after; SadBot overrides this
}

bool ABot::idleSpecial(Server& server, std::time_t now) {
  (void)server;
  (void)now;
  return false;
}

std::string ABot::flavourFor(const std::string& nick, const std::string& text) const {
  //< A bot that talks to everybody identically has no relationships worth
  //< tracking. This is the cheapest place they become audible.
  if (_brain->likes(nick) && roll() < 0.30f) return text + " — good to hear from you";
  if (_brain->dislikes(nick) && roll() < 0.30f) return text + ".";
  return text;
}

}  // namespace Bots
