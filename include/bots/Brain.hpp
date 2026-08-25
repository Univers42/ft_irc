#ifndef BOTS_BRAIN_HPP
#define BOTS_BRAIN_HPP

#include <ctime>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "bots/Emotion.hpp"

/*
** Brain — everything a bot KNOWS and REMEMBERS.
**
** IS-A versus HAS-A, which is the point of this file:
**
**     JokerBot IS-A ABot      inheritance — it varies the VOICE
**     ABot     HAS-A Brain    composition — the Brain is the COGNITION
**
** Personality is the subclass; cognition is the object it points at. The two
** change for different reasons: adding a bot should mean writing a subclass
** with some lines and a couple of overridden hooks, NOT re-implementing
** belief tracking, escalation memory or mood. Those live here once and every
** bot improves together.
**
** It also makes the brain SWAPPABLE. `ABot` owns a `Brain*`, so a bot can be
** handed a different one -- a duller brain, an instrumented one for tests --
** without touching a single subclass. That is the payoff composition buys
** that a base-class member would not.
**
** WHAT THE BRAIN MAY NOT DO
** -------------------------
** Invent privilege. `isOpIn()` answers only from what the SERVER has shown
** (a +o we witnessed, or membership the server confirmed). Nothing here
** consults a bot's configured role, so a bot can never talk itself into
** believing it may kick.
*/
namespace Bots {

//< One channel's live conversation. The CHANNEL, not the bot, is the unit of
//< state -- without that nothing ever builds, and two bots never take three
//< turns at each other.
struct Thread {
  std::string channel;
  std::string lastSpeaker;
  std::string addressee;  //< who the last line was aimed at
  std::vector<std::string> topic;
  std::string openQuestion;
  std::string questionFrom;
  std::time_t lastAt;
  std::time_t questionAt;
  float heat;  //< 0 calm .. 1 flame war
  std::vector<std::time_t> recent;

  Thread();

  void record(const std::string& nick, const std::vector<std::string>& kw, bool question, const std::string& aimedAt,
              float hostility, std::time_t now);
  void cool();
  bool isLive(std::time_t now) const;
  bool questionOpen(std::time_t now) const;
  int rate(std::time_t now, int window) const;  //< messages in the last N seconds
};

class Brain {
 public:
  Brain(const std::string& nick, const Temperament& temperament, float sensitivity);
  virtual ~Brain();

  //< What one message turned out to be, once this brain has read it.
  struct Reading {
    Vector felt;
    float impact;  //< hostility x sensitivity — drives the ladder
    Axis dominant;
    float strength;
    bool atMe;
    bool aboutMe;
    bool question;
    std::string phrase;  //< the worst word, for the log
    std::string addressee;
    std::vector<std::string> keywords;
  };

  //< Read a line through THIS brain's temperament. Virtual so a subclass can
  //< be duller or sharper without any bot knowing.
  virtual Reading read(const std::string& text, const std::string& speaker, const std::string& channel,
                       std::time_t now);

  // -- belief, fed ONLY by observed server events ------------------------
  void sawJoin(const std::string& channel, const std::string& nick);
  void sawPart(const std::string& channel, const std::string& nick);
  void sawOp(const std::string& channel, const std::string& nick, bool granted);
  void sawMode(const std::string& channel, char flag, bool set);
  void deniedIn(const std::string& channel);

  bool isOpIn(const std::string& channel) const;  //< the ONLY privilege authority
  bool isOp(const std::string& channel, const std::string& nick) const;
  bool inChannel(const std::string& channel) const;
  bool wasDenied(const std::string& channel) const;
  bool hasMode(const std::string& channel, char flag) const;
  std::vector<std::string> peers(const std::string& channel) const;
  std::vector<std::string> channels() const;

  // -- feeling ------------------------------------------------------------
  void woundedBy(const std::string& speaker, float impact, float grudge);
  void warmedBy(const std::string& speaker, int amount);
  void settle();  //< one tick of forgetting
  float mood() const { return _mood; }
  std::string moodWord() const;

  // -- relationships ------------------------------------------------------
  int feelingToward(const std::string& nick) const;
  void adjust(const std::string& nick, int delta);
  bool likes(const std::string& nick) const;
  bool dislikes(const std::string& nick) const;

  // -- the escalation ladder ---------------------------------------------
  //< Which rung this speaker has earned, and how far we already went. The
  //< BOT decides what a rung MEANS; how far up somebody is, is a fact about
  //< accumulated behaviour and belongs here.
  int rungFor(const std::string& speaker, float warnAt, float finalAt, float kickAt) const;
  int rungUsed(const std::string& speaker) const;
  void climbed(const std::string& speaker, int rung);
  float pressureFrom(const std::string& speaker) const;

  // -- conversation -------------------------------------------------------
  Thread& thread(const std::string& channel);
  const Thread* peekThread(const std::string& channel) const;
  //< How strongly this channel is pulling us in. The single place density is
  //< tuned, and the single place it is capped.
  float urgeToSpeak(const std::string& channel, float base, std::time_t now) const;

  // -- pacing -------------------------------------------------------------
  bool onCooldown(std::time_t now, float seconds) const;
  void noteSpoke(std::time_t now);
  std::time_t lastSpoke() const { return _lastSpoke; }
  std::time_t lastAction() const { return _lastAction; }
  void noteAction(std::time_t now) { _lastAction = now; }

  const std::string& nick() const { return _nick; }
  void setNick(const std::string& n) { _nick = n; }

  int spoken() const { return _spoken; }
  int attempted() const { return _attempted; }
  int refused() const { return _refused; }
  void countAttempt() { ++_attempted; }
  void countRefusal() { ++_refused; }

 protected:
  std::string _nick;
  Temperament _temperament;
  float _sensitivity;

  Vector _weather;  //< slow-moving emotional average
  float _mood;
  float _baseline;

  std::map<std::string, std::set<std::string> > _members;
  std::map<std::string, std::set<std::string> > _ops;
  std::map<std::string, std::set<char> > _modes;
  std::set<std::string> _channels;
  std::set<std::string> _denied;
  std::set<std::string> _known;

  std::map<std::string, int> _relations;
  std::map<std::string, float> _pressure;
  std::map<std::string, int> _rung;

  std::map<std::string, Thread> _threads;

  std::time_t _lastSpoke;
  std::time_t _lastAction;
  int _spoken;
  int _attempted;
  int _refused;

 private:
  Brain(const Brain& other);
  Brain& operator=(const Brain& other);
};

/*
** A deliberately duller brain, to prove the intelligence lives here.
**
** Same bots, same voices, visibly less going on: it forgets grudges the
** moment they are made and feels no pull toward a live thread. Give a
** JokerBot one and it still tells the same jokes -- into the void, at random.
** The cheapest possible regression check on the claim that composition
** bought us anything.
*/
class SimpleBrain : public Brain {
 public:
  SimpleBrain(const std::string& nick, const Temperament& t, float sensitivity);
  Reading read(const std::string& text, const std::string& speaker, const std::string& channel, std::time_t now);
};

}  // namespace Bots

#endif
