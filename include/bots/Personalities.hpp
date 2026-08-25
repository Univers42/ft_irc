#ifndef BOTS_PERSONALITIES_HPP
#define BOTS_PERSONALITIES_HPP

#include <string>

#include "bots/ABot.hpp"

/*
** The cast. Eight classes, each IS-A ABot, each HAS-A Brain with its own
** Temperament.
**
** Every one of them is the same twenty lines of shape: a constructor that
** fills in the character sheet, and two to four overridden hooks. That is
** the measure of whether the base class earned its keep -- if adding a bot
** needed more than this, the machinery would be in the wrong place.
**
** What actually distinguishes them is the TEMPERAMENT plus the ladder
** thresholds. GrumpyBot at irritable()/warnAt 1.8 and JokerBot at
** flippant()/warnAt 5.0 read one identical sentence roughly six times apart,
** which is why one warns and the other makes a joke without either of them
** containing a special case for the other.
*/
namespace Bots {

class JokerBot : public ABot {
 public:
  JokerBot(Server* server, const std::string& nick, const std::string& role);

 protected:
  std::string onInsult(const Brain::Reading& r, const std::string& speaker);
  std::string warningLine(const std::string& speaker, bool isFinal) const;
  std::string kickReason() const;
  std::string idleLine() const;
  std::string answerLine() const;
  std::string greetLine(const std::string& who) const;
  std::string onMentioned(const std::string& speaker);
  std::string onThanked(const std::string& speaker);
  float greetOdds() const;
};

class SadBot : public ABot {
 public:
  SadBot(Server* server, const std::string& nick, const std::string& role);

 protected:
  std::string onInsult(const Brain::Reading& r, const std::string& speaker);
  std::string warningLine(const std::string& speaker, bool isFinal) const;
  std::string idleLine() const;
  std::string answerLine() const;
  float greetOdds() const;
  std::string greetLine(const std::string& who) const;
  std::string onSympathy(const std::string& speaker);
  std::string onMentioned(const std::string& speaker);
  //< Rung 4 for a bot that cannot kick: it LEAVES. The honest version of
  //< powerlessness -- a real IRC action with a real consequence, rather than
  //< pretending to moderate.
  void sanction(Server& server, const std::string& channel, const std::string& speaker);
};

class HappyBot : public ABot {
 public:
  HappyBot(Server* server, const std::string& nick, const std::string& role);

 protected:
  std::string onInsult(const Brain::Reading& r, const std::string& speaker);
  std::string warningLine(const std::string& speaker, bool isFinal) const;
  std::string idleLine() const;
  std::string answerLine() const;
  std::string greetLine(const std::string& who) const;
  std::string onThanked(const std::string& speaker);
  //< Checks on whoever was targeted, not just the room.
  float greetOdds() const;
};

class GrumpyBot : public ABot {
 public:
  GrumpyBot(Server* server, const std::string& nick, const std::string& role);

 protected:
  std::string onInsult(const Brain::Reading& r, const std::string& speaker);
  std::string warningLine(const std::string& speaker, bool isFinal) const;
  std::string kickReason() const;
  std::string idleLine() const;
  std::string answerLine() const;
  std::string greetLine(const std::string& who) const;
  float greetOdds() const;
};

class OverexcitedBot : public ABot {
 public:
  OverexcitedBot(Server* server, const std::string& nick, const std::string& role);

 protected:
  std::string onInsult(const Brain::Reading& r, const std::string& speaker);
  std::string idleLine() const;
  std::string answerLine() const;
  std::string greetLine(const std::string& who) const;
  float greetOdds() const;
};

class CalmBot : public ABot {
 public:
  CalmBot(Server* server, const std::string& nick, const std::string& role);

 protected:
  std::string onInsult(const Brain::Reading& r, const std::string& speaker);
  std::string idleLine() const;
  std::string answerLine() const;
};

class FileBot : public ABot {
 public:
  FileBot(Server* server, const std::string& nick, const std::string& role);

 protected:
  std::string onInsult(const Brain::Reading& r, const std::string& speaker);
  //< A file request outranks everything, including being insulted.
  std::string onAddressed(const Brain::Reading& r, const std::string& speaker, const std::string& text);
  std::string onAmbient(const std::string& channel);
  std::string idleLine() const;
  std::string answerLine() const;
  //< Announces new artefacts instead of making small talk.
  bool idleSpecial(Server& server, std::time_t now);

 private:
  std::string artefact() const;
  mutable int _counter;
};

class OperatorBot : public ABot {
 public:
  OperatorBot(Server* server, const std::string& nick, const std::string& role);

 protected:
  std::string onInsult(const Brain::Reading& r, const std::string& speaker);
  std::string warningLine(const std::string& speaker, bool isFinal) const;
  std::string kickReason() const;
  std::string idleLine() const;
  std::string answerLine() const;
  std::string greetLine(const std::string& who) const;
  //< A quiet sweep for channels left locked down, and for regulars worth
  //< trusting. Duty rather than coincidence.
  bool idleSpecial(Server& server, std::time_t now);
};

//< Factory: a config word -> a bot. The only place the concrete classes are
//< named, so adding a ninth means one file and one line here.
ABot* make(const std::string& personality, Server* server, const std::string& nick, const std::string& role);

}  // namespace Bots

#endif
