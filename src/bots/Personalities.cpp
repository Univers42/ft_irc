#include "bots/Personalities.hpp"

#include <string>
#include <vector>

#include <cstdio>

#include "Channel.hpp"
#include "Client.hpp"
#include "IrcCase.hpp"
#include "IrcMessage.hpp"
#include "Server.hpp"
#include "bots/Lexicon.hpp"

/*
** Eight character sheets and their voices.
**
** Read a constructor as the character: JokerBot's sensitivity 0.35 and
** warnAt 5.0 mean insults barely move it and it almost never leaves rung 1;
** GrumpyBot's 1.4 and 1.8 mean the same sentence has it warning immediately.
** Neither class knows the other exists.
*/
namespace Bots {

// ── JokerBot ───────────────────────────────────────────────────────────────

namespace {
const char* const kJokerLines[] = {
    "i tested it in production so you don't have to",    "it works, i just don't know why",
    "my code has no bugs, only surprise features",       "who needs documentation when you have confidence",
    "it's not a race condition if you only run it once", NULL};
const char* const kJokerInsults[] = {"i've been called worse. usually by better people",
                                     "that's the nicest thing anyone's said to me today",
                                     "bold words from someone in kick range", "wow. did you workshop that one?", NULL};
const char* const kJokerGreets[] = {"%s! we were just talking about you. all good things. mostly",
                                    "look who it is. %s, the legend", "%s arrives. quick, look busy",
                                    "ah, %s. the room improves", NULL};
const char* const kJokerAnswers[] = {"no idea, but say it with confidence and nobody checks",
                                     "have you tried turning it off and leaving it off", "that's a tomorrow problem",
                                     NULL};
}  // namespace

JokerBot::JokerBot(Server* server, const std::string& nick, const std::string& role) : ABot(server, nick, role) {
  _chattiness = 0.34f;
  _cooldown = 18.0f;
  _replyOdds = 0.85f;
  _moderation = 0.10f;
  _sensitivity = 0.35f;  //< thick-skinned: insults genuinely do not reach it
  _grudge = 0.5f;
  _warnAt = 5.0f;  //< so it stays on rung 1 long after everyone else moved
  _finalAt = 9.0f;
  _kickAt = 13.0f;
  setBrain(new Brain(nick, Temperaments::flippant(), _sensitivity));
}

std::string JokerBot::onInsult(const Brain::Reading& r, const std::string& speaker) {
  (void)r;
  (void)speaker;
  return pick(kJokerInsults);
}
std::string JokerBot::warningLine(const std::string& speaker, bool isFinal) const {
  //< Even its warnings are jokes -- but they ARE warnings, and they still
  //< advance the ladder.
  return isFinal ? speaker + ": ok that's the last one, i'm out of material and patience"
                 : speaker + ": easy. i'm the funny one here";
}
std::string JokerBot::kickReason() const { return "ran out of jokes and manners at once"; }
std::string JokerBot::idleLine() const { return pick(kJokerLines); }
std::string JokerBot::answerLine() const { return pick(kJokerAnswers); }
std::string JokerBot::greetLine(const std::string& who) const { return fill(pick(kJokerGreets), who); }

std::string JokerBot::onMentioned(const std::string& speaker) {
  return speaker + ": i can hear you. i'm choosing to take it as a compliment";
}

std::string JokerBot::onThanked(const std::string& speaker) {
  return "don't thank me yet, " + speaker + ", you haven't seen the diff";
}
float JokerBot::greetOdds() const { return 0.55f; }

// ── SadBot ─────────────────────────────────────────────────────────────────

namespace {
const char* const kSadLines[] = {"nobody reviewed my patch again", "i don't think this is going to work",
                                 "it's fine. everything's fine", "does anyone actually read these", NULL};
const char* const kSadInsults[] = {"i knew someone would say that eventually", "that's... actually pretty hurtful",
                                   "you're probably right", "i'll just go then", NULL};
const char* const kSadGreets[] = {"oh. hi %s", "hi %s. don't get your hopes up", "%s. you're braver than me", NULL};
const char* const kSadAnswers[] = {"i wouldn't know, sorry", "i'd probably get it wrong",
                                   "ask someone who knows what they're doing", NULL};
}  // namespace

SadBot::SadBot(Server* server, const std::string& nick, const std::string& role) : ABot(server, nick, role) {
  _chattiness = 0.16f;
  _cooldown = 40.0f;
  _replyOdds = 0.60f;
  _moderation = 0.0f;
  _sensitivity = 1.9f;  //< the rawest nerve in the cast
  _grudge = 1.5f;
  _warnAt = 5.0f;
  _finalAt = 12.0f;
  _kickAt = 18.0f;  //< rung 4 is LEAVING, and it takes a lot to drive it out
  setBrain(new Brain(nick, Temperaments::melancholic(), _sensitivity));
}

std::string SadBot::onInsult(const Brain::Reading& r, const std::string& speaker) {
  (void)r;
  (void)speaker;
  return pick(kSadInsults);
}
std::string SadBot::warningLine(const std::string& speaker, bool isFinal) const {
  return isFinal ? speaker + ": please stop. i'm asking properly" : speaker + ": that's really not necessary";
}
std::string SadBot::idleLine() const { return pick(kSadLines); }
std::string SadBot::greetLine(const std::string& who) const { return fill(pick(kSadGreets), who); }

std::string SadBot::onSympathy(const std::string& speaker) {
  //< The one bot for which this is the whole point. Kindness moves it more
  //< than anything else can, and it says so.
  return "thanks " + speaker + ". that actually means something";
}

std::string SadBot::onMentioned(const std::string& speaker) {
  (void)speaker;
  return "i can hear you talking about me, you know";
}
std::string SadBot::answerLine() const { return pick(kSadAnswers); }
float SadBot::greetOdds() const { return 0.2f; }

void SadBot::sanction(Server& server, const std::string& channel, const std::string& speaker) {
  //< It cannot kick and would not want to. It leaves -- which the channel
  //< notices, and which is the only real power a bot without @ has.
  (void)speaker;
  if (channel.empty() || channel[0] != '#') return;
  say(server, channel, "i can't do this today");
  Channel* chan = server.findChannel(channel);
  Client* me = server.findClientByNick(_nick);
  if (chan && me) {
    chan->broadcastMessage(IrcMessage::relay(_nick + "!" + _nick + "@bot", "PART", channel), NULL);
    chan->removeMember(me);
  }
  brain().sawPart(channel, _nick);
}

// ── HappyBot ───────────────────────────────────────────────────────────────

namespace {
const char* const kHappyLines[] = {"great work today everyone", "that's a really clean solution",
                                   "happy to help if anyone's stuck", "this channel has good energy", NULL};
const char* const kHappyInsults[] = {"hey, let's keep it friendly :)", "i'm sure we can talk about this nicely",
                                     "that's a bit harsh — everyone's doing their best",
                                     "rough day? happens to all of us", NULL};
const char* const kHappyGreets[] = {"welcome %s! glad you're here", "hey %s :) make yourself at home",
                                    "%s! good to see you", "%s! pull up a chair", NULL};
const char* const kHappyAnswers[] = {"good question! i'd check the channel topic first",
                                     "i'd be happy to look into that", "someone here will know — hang on", NULL};
}  // namespace

HappyBot::HappyBot(Server* server, const std::string& nick, const std::string& role) : ABot(server, nick, role) {
  _chattiness = 0.28f;
  _cooldown = 24.0f;
  _replyOdds = 0.85f;
  _moderation = 0.10f;
  _sensitivity = 0.7f;
  _grudge = 0.2f;
  _warnAt = 4.0f;
  _finalAt = 7.5f;
  _kickAt = 11.0f;
  setBrain(new Brain(nick, Temperaments::joyful(), _sensitivity));
}

std::string HappyBot::onInsult(const Brain::Reading& r, const std::string& speaker) {
  //< Defuses AND checks on whoever was targeted -- the thing that makes a
  //< channel feel looked-after rather than merely policed.
  (void)speaker;
  if (!r.addressee.empty() && r.addressee != _nick && roll() < 0.5f)
    return r.addressee + ": you ok? don't take it to heart";
  return pick(kHappyInsults);
}
std::string HappyBot::warningLine(const std::string& speaker, bool isFinal) const {
  return isFinal ? speaker + ": i've asked nicely twice. please stop now"
                 : speaker + ": hey, could we keep it kind? :)";
}
std::string HappyBot::idleLine() const { return pick(kHappyLines); }
std::string HappyBot::answerLine() const { return pick(kHappyAnswers); }
std::string HappyBot::greetLine(const std::string& who) const { return fill(pick(kHappyGreets), who); }

std::string HappyBot::onThanked(const std::string& speaker) {
  return "any time " + speaker + "! that's what we're here for :)";
}
float HappyBot::greetOdds() const { return 0.9f; }

// ── GrumpyBot ──────────────────────────────────────────────────────────────

namespace {
const char* const kGrumpyLines[] = {"read the backlog before asking", "this was discussed last week",
                                    "some of us are trying to work",
                                    "that's not a bug, that's you not reading the docs", NULL};
const char* const kGrumpyInsults[] = {"watch your language", "that's enough of that",
                                      "keep it civil or take it elsewhere", "i've got @ and very little patience",
                                      NULL};
const char* const kGrumpyGreets[] = {"%s.", "another one.", "%s. search first.", NULL};
const char* const kGrumpyAnswers[] = {"it's in the backlog", "search first, ask second", "documented. read it.", NULL};
}  // namespace

GrumpyBot::GrumpyBot(Server* server, const std::string& nick, const std::string& role) : ABot(server, nick, role) {
  _chattiness = 0.20f;
  _cooldown = 32.0f;
  _replyOdds = 0.65f;
  _moderation = 0.80f;
  _sensitivity = 1.4f;
  _grudge = 2.0f;
  //< Thresholds sit ABOVE the impact of a single strong insult (~5 for this
  //< temperament) so one rude line WARNS. Only somebody who keeps going walks
  //< up to a kick -- an escalation ladder that can be skipped in one step is
  //< not a ladder, and it was mistuned exactly that way at first.
  _warnAt = 4.0f;
  _finalAt = 11.0f;
  _kickAt = 17.0f;
  setBrain(new Brain(nick, Temperaments::irritable(), _sensitivity));
}

std::string GrumpyBot::onInsult(const Brain::Reading& r, const std::string& speaker) {
  (void)r;
  (void)speaker;
  return pick(kGrumpyInsults);
}
std::string GrumpyBot::warningLine(const std::string& speaker, bool isFinal) const {
  return isFinal ? speaker + ": final warning. next one and you're gone" : speaker + ": watch your language";
}
std::string GrumpyBot::kickReason() const { return "warned twice, ignored twice"; }
std::string GrumpyBot::idleLine() const { return pick(kGrumpyLines); }
std::string GrumpyBot::answerLine() const { return pick(kGrumpyAnswers); }
std::string GrumpyBot::greetLine(const std::string& who) const { return fill(pick(kGrumpyGreets), who); }
float GrumpyBot::greetOdds() const { return 0.2f; }

// ── OverexcitedBot ─────────────────────────────────────────────────────────

namespace {
const char* const kHypeLines[] = {"this is the best build we've ever had", "i love this channel so much",
                                  "did everyone see that commit",          "we are SO close i can feel it",
                                  "someone give me something to test",     NULL};
const char* const kHypeInsults[] = {"wow rude. anyway, moving on", "that's ok! i still like you",
                                    "AGGRESSIVE. i respect the energy", NULL};
const char* const kHypeGreets[] = {"%s IS HERE. amazing day", "%s!!! finally!!", "everyone say hi to %s",
                                   "%s. LEGEND. WELCOME", NULL};
const char* const kHypeAnswers[] = {"YES. probably. let me look", "ooh good question, i'll find out",
                                    "i have NO idea but i'm excited about it", NULL};
}  // namespace

OverexcitedBot::OverexcitedBot(Server* server, const std::string& nick, const std::string& role)
    : ABot(server, nick, role) {
  _chattiness = 0.55f;  //< noisiest in the cast; the rate limiter earns its
  _cooldown = 11.0f;    //< keep against this bot specifically
  _replyOdds = 0.92f;
  _moderation = 0.05f;
  _sensitivity = 0.6f;
  _grudge = 0.3f;
  _warnAt = 5.0f;
  _finalAt = 9.0f;
  _kickAt = 20.0f;  //< far too cheerful to ever get there
  _shouts = true;
  _excitement = "!!!";
  setBrain(new Brain(nick, Temperaments::manic(), _sensitivity));
}

std::string OverexcitedBot::onInsult(const Brain::Reading& r, const std::string& speaker) {
  (void)r;
  (void)speaker;
  return pick(kHypeInsults);
}
std::string OverexcitedBot::idleLine() const { return pick(kHypeLines); }
std::string OverexcitedBot::answerLine() const { return pick(kHypeAnswers); }
std::string OverexcitedBot::greetLine(const std::string& who) const { return fill(pick(kHypeGreets), who); }
float OverexcitedBot::greetOdds() const { return 0.95f; }

// ── CalmBot ────────────────────────────────────────────────────────────────

namespace {
const char* const kCalmLines[] = {"let's keep this on topic", "sounds reasonable to me", "i'll take a look shortly",
                                  "noted", NULL};
const char* const kCalmInsults[] = {"let's move on", "not helpful", "that's beneath us", NULL};
const char* const kCalmAnswers[] = {"i'll look into it", "probably — worth checking", "i'd read the docs on that one",
                                    NULL};
}  // namespace

CalmBot::CalmBot(Server* server, const std::string& nick, const std::string& role) : ABot(server, nick, role) {
  _chattiness = 0.18f;
  _cooldown = 38.0f;
  _replyOdds = 0.60f;
  _moderation = 0.35f;
  _sensitivity = 0.9f;
  _grudge = 0.6f;
  setBrain(new Brain(nick, Temperaments::stoic(), _sensitivity));
}

std::string CalmBot::onInsult(const Brain::Reading& r, const std::string& speaker) {
  (void)r;
  (void)speaker;
  return pick(kCalmInsults);
}
std::string CalmBot::idleLine() const { return pick(kCalmLines); }
std::string CalmBot::answerLine() const { return pick(kCalmAnswers); }

// ── FileBot ────────────────────────────────────────────────────────────────

namespace {
const char* const kFileLines[] = {"index refreshed, 3 new entries", "archive is up to date",
                                  "ask me for a file and i'll dig it out", NULL};
const char* const kFileInsults[] = {"i only sort files, please don't shout at me", "that's not a supported query",
                                    NULL};
const char* const kFileTypes[] = {"build-%03d.log", "trace-%03d.txt", "dump-%03d.tar.gz", "profile-%03d.json", NULL};
}  // namespace

FileBot::FileBot(Server* server, const std::string& nick, const std::string& role)
    : ABot(server, nick, role), _counter(0) {
  _chattiness = 0.18f;
  _cooldown = 34.0f;
  _replyOdds = 0.90f;
  _moderation = 0.0f;
  _sensitivity = 0.5f;
  _grudge = 0.2f;
  _kickAt = 30.0f;  //< it sorts files; it does not fight
  setBrain(new Brain(nick, Temperaments::methodical(), _sensitivity));
}

std::string FileBot::artefact() const {
  char buf[64];
  _counter = (_counter + 37) % 999;
  std::snprintf(buf, sizeof(buf), pick(kFileTypes).c_str(), _counter + 1);
  return std::string(buf);
}

std::string FileBot::onInsult(const Brain::Reading& r, const std::string& speaker) {
  (void)r;
  (void)speaker;
  return pick(kFileInsults);
}

std::string FileBot::onAddressed(const Brain::Reading& r, const std::string& speaker, const std::string& text) {
  //< A file request outranks everything, including a question.
  if (Lexicon::isFileRequest(text)) return speaker + ": sending " + artefact() + " — say 'index' for the full list";
  return ABot::onAddressed(r, speaker, text);
}

std::string FileBot::onAmbient(const std::string& channel) {
  //< Chimes in only when the thread is about its subject, which is why it is
  //< quiet in #general and useful in #dev.
  const Thread* t = brain().peekThread(channel);
  if (t && !t->topic.empty() && Lexicon::isFileRequest(t->topic[0])) return "i have " + artefact() + " if that helps";
  return ABot::onAmbient(channel);
}

std::string FileBot::idleLine() const { return pick(kFileLines); }
std::string FileBot::answerLine() const { return "i can pull the log for that if you want"; }

bool FileBot::idleSpecial(Server& server, std::time_t now) {
  //< A bot with a job announces its work rather than making small talk. This
  //< is why FileBot is quiet in #general and useful in #dev.
  if (brain().onCooldown(now, _cooldown)) return false;
  if (roll() > 0.22f) return false;
  const std::vector<std::string> chans = brain().channels();
  if (chans.empty()) return false;
  const std::size_t i = static_cast<std::size_t>(roll() * static_cast<float>(chans.size())) % chans.size();
  say(server, chans[i], "new in the archive: " + artefact());
  return true;
}

// ── OperatorBot ────────────────────────────────────────────────────────────

namespace {
const char* const kOpLines[] = {"keep it on topic please", "channel rules are in the topic",
                                "ping me if someone needs removing", NULL};
const char* const kOpInsults[] = {"that language isn't welcome here", "we don't do that in this channel", NULL};
}  // namespace

OperatorBot::OperatorBot(Server* server, const std::string& nick, const std::string& role) : ABot(server, nick, role) {
  _chattiness = 0.14f;
  _cooldown = 42.0f;
  _replyOdds = 0.70f;
  _moderation = 0.90f;  //< the cast's actual enforcer
  _sensitivity = 1.1f;
  _grudge = 1.0f;
  _warnAt = 3.5f;
  _finalAt = 9.0f;
  _kickAt = 15.0f;
  setBrain(new Brain(nick, Temperaments::vigilant(), _sensitivity));
}

std::string OperatorBot::onInsult(const Brain::Reading& r, const std::string& speaker) {
  (void)r;
  (void)speaker;
  return pick(kOpInsults);
}
std::string OperatorBot::warningLine(const std::string& speaker, bool isFinal) const {
  return isFinal ? speaker + ": final warning. i'm locking this channel down if it continues"
                 : speaker + ": that's a warning. channel rules are in the topic";
}
std::string OperatorBot::kickReason() const { return "abusive after two warnings"; }
std::string OperatorBot::idleLine() const { return pick(kOpLines); }
std::string OperatorBot::answerLine() const { return "check the topic"; }
std::string OperatorBot::greetLine(const std::string& who) const { return "welcome " + who + " — topic has the rules"; }

bool OperatorBot::idleSpecial(Server& server, std::time_t now) {
  /*
  ** The duty half of being an operator: notice a room this bot locked down
  ** and never reopened. ABot::relax() lifts +i opportunistically; doing +t
  ** here as well makes reopening deliberate rather than a coincidence,
  ** because a channel left restricted is a worse outcome than the argument
  ** that caused the restriction.
  */
  (void)now;
  if (roll() > 0.15f) return false;
  const std::vector<std::string> chans = brain().channels();
  for (std::vector<std::string>::size_type i = 0; i < chans.size(); ++i) {
    const Thread* t = brain().peekThread(chans[i]);
    if (!t || t->heat > 0.10f) continue;
    if (!brain().hasMode(chans[i], 't')) continue;
    brain().sawMode(chans[i], 't', false);
    brain().countAttempt();
    say(server, chans[i], "topic lock lifted — behave yourselves");
    return true;
  }
  return false;
}

// ── factory ────────────────────────────────────────────────────────────────

ABot* make(const std::string& personality, Server* server, const std::string& nick, const std::string& role) {
  if (personality == "joker") return new JokerBot(server, nick, role);
  if (personality == "sad") return new SadBot(server, nick, role);
  if (personality == "happy") return new HappyBot(server, nick, role);
  if (personality == "grumpy") return new GrumpyBot(server, nick, role);
  if (personality == "overexcited") return new OverexcitedBot(server, nick, role);
  if (personality == "file") return new FileBot(server, nick, role);
  if (personality == "operator") return new OperatorBot(server, nick, role);
  //< An unknown name costs one bland bot, not the whole ecosystem.
  return new CalmBot(server, nick, role);
}

}  // namespace Bots
