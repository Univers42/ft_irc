#ifndef BOTS_ABOT_HPP
#define BOTS_ABOT_HPP

#include <string>
#include <vector>

#include "bots/Brain.hpp"
#include "bots/Emotion.hpp"
#include "ext/IServerExtension.hpp"

class Server;
class Client;
class Channel;

/*
** ABot — the algorithm every bot shares, and the hooks each one bends.
**
**     ABot     IS-A  IServerExtension   it lives inside the server
**     JokerBot IS-A  ABot               inheritance varies the VOICE
**     ABot     HAS-A Brain*             composition holds the COGNITION
**
** TEMPLATE METHOD
** ---------------
** onPrivmsg() is fixed and NOT virtual to subclasses in spirit -- they never
** touch it. It always runs the same sequence:
**
**     brain->read(text)        how it feels, through this bot's temperament
**       -> escalate()          if there was hostility
**       -> onAddressed()       if spoken to
**       -> onAmbient()         if the thread pulls hard enough
**       -> nothing             which is the common case, by a wide margin
**
** Subclasses override the HOOKS -- onInsult, onAddressed, onAmbient, onJoin,
** idleLine, warningLine, sanction -- and set their character sheet in the
** constructor. That is the whole extension surface, which is why a new bot
** cannot break the observe/decide/act contract: it does not implement it.
**
** WHY THESE LIVE INSIDE THE SERVER
** --------------------------------
** The Python ecosystem drives real sockets from outside, which is the right
** way to TEST the server. These are the same design as in-process residents:
** no new file descriptors, no I/O of their own, no threads -- they observe
** through the extension callbacks the server already dispatches and act by
** queueing messages the same way any command handler does. That keeps the
** subject's one-poll rule untouched, which a second socket would not.
**
** THE ESCALATION LADDER
** ---------------------
** Moderation is a sequence, not a switch:
**
**     1  react in character      the personality's own voice
**     2  name them and warn
**     3  final warning — and if opped, tighten the room (+t, then +i)
**     4  the bot's ultimate sanction
**
** Rung 4 is deliberately virtual. GrumpyBot kicks; SadBot, which can never
** kick anything, PARTs instead. Same ladder, different ultimate act -- which
** is what makes powerlessness legible rather than merely absent.
**
** PRIVILEGE
** ---------
** No hook may consult the bot's ROLE to decide whether an action will
** SUCCEED. Role decides what it is willing to ATTEMPT; only
** brain->isOpIn() -- fed from observed MODE traffic -- describes what the
** server has granted, and even that is a belief the server may contradict.
*/
namespace Bots {

class ABot : public IServerExtension {
 public:
  ABot(Server* server, const std::string& nick, const std::string& role);
  virtual ~ABot();

  // -- IServerExtension ---------------------------------------------------
  virtual const char* name() const;
  virtual bool onPrivmsg(Server& server, Client& sender, const std::string& target, const std::string& text);
  virtual void onJoin(Server& server, Client& client, Channel& channel);
  virtual void onPart(Server& server, Client& client, Channel& channel);
  virtual void onTick(Server& server, std::time_t now);
  virtual bool reservesNick(const std::string& nick) const;

  const std::string& nick() const { return _nick; }
  const std::string& role() const { return _role; }
  Brain& brain() { return *_brain; }
  const Brain& brain() const { return *_brain; }

  //< Hand this bot a different mind. Takes ownership; the old one is freed.
  //< This is what composition buys that a base-class member would not.
  void setBrain(Brain* brain);

 protected:
  // ── the character sheet ───────────────────────────────────────────────
  //< Set by each subclass in its constructor. Numbers, not behaviour --
  //< behaviour is the hooks below.
  float _chattiness;
  float _cooldown;
  float _replyOdds;
  float _moderation;
  float _sensitivity;
  float _grudge;
  float _warnAt;
  float _finalAt;
  float _kickAt;
  bool _shouts;
  std::string _excitement;

  // ── HOOKS — what a subclass actually writes ───────────────────────────
  virtual std::string onInsult(const Brain::Reading& r, const std::string& speaker) = 0;
  virtual std::string warningLine(const std::string& speaker, bool isFinal) const;
  virtual std::string kickReason() const;
  virtual std::string onAddressed(const Brain::Reading& r, const std::string& speaker, const std::string& text);
  virtual std::string onAmbient(const std::string& channel);
  virtual std::string greetLine(const std::string& who) const;
  virtual std::string idleLine() const = 0;
  virtual std::string answerLine() const;
  virtual float greetOdds() const;

  //< Somebody was talking ABOUT this bot to somebody else, rather than to it.
  virtual std::string onMentioned(const std::string& speaker);
  //< Warmth. Kept separate from onAddressed because a thank-you and a
  //< question deserve genuinely different answers, and folding them together
  //< is what makes a bot reply to gratitude with a shrug.
  virtual std::string onThanked(const std::string& speaker);
  virtual std::string onSympathy(const std::string& speaker);
  //< A bot with a JOB: FileBot announces artefacts, OperatorBot sweeps for
  //< channels it locked down and never reopened. Runs before idle chatter,
  //< so a working bot is not merely a chatty one.
  virtual bool idleSpecial(Server& server, std::time_t now);
  //< Colours a line by how this bot feels about the person it is talking to.
  virtual std::string flavourFor(const std::string& nick, const std::string& text) const;

  //< Rung 4. Default is a kick; SadBot overrides it to leave instead.
  virtual void sanction(Server& server, const std::string& channel, const std::string& speaker);

  // ── shared machinery ──────────────────────────────────────────────────
  void say(Server& server, const std::string& target, const std::string& text);
  std::string style(const std::string& text) const;
  std::string pick(const char* const* pool) const;

  //< Walks the ladder. Not virtual -- every bot escalates the same way and
  //< only differs in what each rung SAYS and what rung 4 DOES.
  void escalate(Server& server, const std::string& channel, const std::string& speaker, const Brain::Reading& r);
  //< Should this bot be the one to speak up? Everyone FEELS the insult;
  //< this only decides who answers, so eleven bots do not pile onto one line.
  bool mayReact(const std::string& channel, const std::string& speaker, const Brain::Reading& r) const;
  void tighten(Server& server, const std::string& channel);
  //< The other half of tighten(): give a calmed channel its modes back. A bot
  //< that only ever locks rooms down leaves every channel permanently +i,
  //< which is a worse outcome than never moderating at all.
  bool relax(Server& server, const std::string& channel);

  Server* _server;
  Brain* _brain;  //< HAS-A, owned
  std::string _nick;
  std::string _role;
  mutable unsigned int _seed;  //< tiny LCG; no <random> in C++98

  float roll() const;  //< 0..1

 private:
  ABot(const ABot& other);
  ABot& operator=(const ABot& other);
};

}  // namespace Bots

#endif
