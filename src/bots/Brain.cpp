#include "bots/Brain.hpp"

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <cstdio>

#include "bots/Lexicon.hpp"

namespace Bots {

// ── Thread ─────────────────────────────────────────────────────────────────

Thread::Thread() : lastAt(0), questionAt(0), heat(0.0f) {}

void Thread::record(const std::string& nick, const std::vector<std::string>& kw, bool question,
                    const std::string& aimedAt, float hostility, std::time_t now) {
  lastSpeaker = nick;
  lastAt = now;
  addressee = aimedAt;
  recent.push_back(now);
  if (recent.size() > 16) recent.erase(recent.begin());

  if (!kw.empty()) {
    //< New keywords lead, old ones linger. A subject that stops being
    //< mentioned falls off the end rather than being wiped, so a brief
    //< digression does not erase what the channel was discussing.
    std::vector<std::string> merged(kw);
    for (std::vector<std::string>::size_type i = 0; i < topic.size() && merged.size() < 5; ++i) {
      bool dup = false;
      for (std::vector<std::string>::size_type j = 0; j < kw.size(); ++j)
        if (kw[j] == topic[i]) dup = true;
      if (!dup) merged.push_back(topic[i]);
    }
    topic = merged;
  }

  if (question) {
    openQuestion = "open";
    questionFrom = nick;
    questionAt = now;
  } else if (!openQuestion.empty() && nick != questionFrom) {
    //< Somebody who is not the asker spoke: treat the question as handled.
    //< Crude, and right often enough -- the alternative is every bot piling
    //< onto a question that was already answered.
    openQuestion.clear();
  }

  heat = hostility > 0.0f ? heat + hostility * 0.18f : heat - 0.04f;
  if (heat > 1.0f) heat = 1.0f;
  if (heat < 0.0f) heat = 0.0f;
}

void Thread::cool() {
  heat -= 0.01f;
  if (heat < 0.0f) heat = 0.0f;
}

bool Thread::isLive(std::time_t now) const { return lastAt && (now - lastAt) < 25; }

bool Thread::questionOpen(std::time_t now) const { return !openQuestion.empty() && (now - questionAt) < 45; }

int Thread::rate(std::time_t now, int window) const {
  int n = 0;
  for (std::vector<std::time_t>::size_type i = 0; i < recent.size(); ++i)
    if (now - recent[i] <= window) ++n;
  return n;
}

// ── Brain ──────────────────────────────────────────────────────────────────

Brain::Brain(const std::string& nick, const Temperament& temperament, float sensitivity)
    : _nick(nick),
      _temperament(temperament),
      _sensitivity(sensitivity),
      _mood(0.0f),
      _baseline(0.0f),
      _lastSpoke(0),
      _lastAction(0),
      _spoken(0),
      _attempted(0),
      _refused(0) {}

Brain::~Brain() {}

Brain::Reading Brain::read(const std::string& text, const std::string& speaker, const std::string& channel,
                           std::time_t now) {
  Reading r;
  r.atMe = Lexicon::addressedTo(text, _nick);
  r.aboutMe = r.atMe;
  r.question = Lexicon::isQuestion(text);
  r.keywords = Lexicon::keywords(text, 3);
  r.phrase = Lexicon::worstPhrase(text);

  //< Read onto the eight axes, then through THIS brain's temperament. Two
  //< bots hearing "you are stupid" genuinely perceive different things.
  const Vector raw = Lexicon::read(text);
  r.felt = _temperament.apply(raw);
  if (r.atMe) r.felt.scale(2.0f);  //< aimed at me lands twice as hard

  _weather.blend(r.felt, 0.1f);
  r.dominant = r.felt.dominant(&r.strength);

  //< Hostility, not overall intensity, drives the ladder: sadness and fear
  //< are unpleasant but not confrontational.
  r.impact = r.felt.hostility() * _sensitivity;

  //< Who else in the room this was aimed at, so a bot can tell it is being
  //< talked ABOUT rather than TO.
  for (std::set<std::string>::const_iterator it = _known.begin(); it != _known.end(); ++it) {
    if (*it == speaker || *it == _nick) continue;
    if (Lexicon::addressedTo(text, *it)) {
      r.addressee = *it;
      break;
    }
  }

  if (!channel.empty() && channel[0] == '#')
    thread(channel).record(speaker, r.keywords, r.question, r.addressee, raw.hostility(), now);

  _known.insert(speaker);

  //< Mood follows the valence of what was just said, so a bot in a friendly
  //< channel drifts cheerful and one in a hostile channel does not.
  _mood += r.felt.valence() * 0.12f;
  if (_mood > 1.0f) _mood = 1.0f;
  if (_mood < -1.0f) _mood = -1.0f;
  return r;
}

void Brain::sawJoin(const std::string& channel, const std::string& nick) {
  _members[channel].insert(nick);
  _known.insert(nick);
  if (nick == _nick) _channels.insert(channel);
}

void Brain::sawPart(const std::string& channel, const std::string& nick) {
  _members[channel].erase(nick);
  _ops[channel].erase(nick);
  if (nick == _nick) _channels.erase(channel);
}

void Brain::sawOp(const std::string& channel, const std::string& nick, bool granted) {
  if (granted)
    _ops[channel].insert(nick);
  else
    _ops[channel].erase(nick);
}

void Brain::sawMode(const std::string& channel, char flag, bool set) {
  if (set)
    _modes[channel].insert(flag);
  else
    _modes[channel].erase(flag);
}

void Brain::deniedIn(const std::string& channel) {
  _denied.insert(channel);
  //< The server just contradicted us. Whatever we thought, we do not hold @
  //< here -- drop the belief rather than keep trying and keep failing.
  _ops[channel].erase(_nick);
}

bool Brain::isOpIn(const std::string& channel) const { return isOp(channel, _nick); }

bool Brain::isOp(const std::string& channel, const std::string& nick) const {
  std::map<std::string, std::set<std::string> >::const_iterator it = _ops.find(channel);
  return it != _ops.end() && it->second.count(nick) > 0;
}

bool Brain::inChannel(const std::string& channel) const { return _channels.count(channel) > 0; }

bool Brain::wasDenied(const std::string& channel) const { return _denied.count(channel) > 0; }

bool Brain::hasMode(const std::string& channel, char flag) const {
  std::map<std::string, std::set<char> >::const_iterator it = _modes.find(channel);
  return it != _modes.end() && it->second.count(flag) > 0;
}

std::vector<std::string> Brain::peers(const std::string& channel) const {
  std::vector<std::string> out;
  std::map<std::string, std::set<std::string> >::const_iterator it = _members.find(channel);
  if (it == _members.end()) return out;
  for (std::set<std::string>::const_iterator m = it->second.begin(); m != it->second.end(); ++m)
    if (*m != _nick) out.push_back(*m);
  return out;
}

std::vector<std::string> Brain::channels() const {
  return std::vector<std::string>(_channels.begin(), _channels.end());
}

void Brain::woundedBy(const std::string& speaker, float impact, float grudge) {
  _pressure[speaker] += impact;
  int step = static_cast<int>(grudge + 0.5f);
  if (step < 1) step = 1;
  adjust(speaker, -step);
  _mood -= 0.12f * (impact > 3.0f ? 3.0f : impact);
  if (_mood < -1.0f) _mood = -1.0f;
}

void Brain::warmedBy(const std::string& speaker, int amount) {
  adjust(speaker, amount);
  _mood += 0.2f * static_cast<float>(amount);
  if (_mood > 1.0f) _mood = 1.0f;
}

void Brain::settle() {
  if (_mood > _baseline)
    _mood = (_mood - 0.02f < _baseline) ? _baseline : _mood - 0.02f;
  else if (_mood < _baseline)
    _mood = (_mood + 0.02f > _baseline) ? _baseline : _mood + 0.02f;

  for (std::map<std::string, Thread>::iterator it = _threads.begin(); it != _threads.end(); ++it) it->second.cool();

  //< Pressure evaporates, so somebody rude an hour ago is not still one word
  //< from a kick. When it drops away the ladder resets with it.
  std::vector<std::string> gone;
  for (std::map<std::string, float>::iterator it = _pressure.begin(); it != _pressure.end(); ++it) {
    it->second *= 0.995f;
    if (it->second < 0.2f) gone.push_back(it->first);
  }
  for (std::vector<std::string>::size_type i = 0; i < gone.size(); ++i) {
    _pressure.erase(gone[i]);
    _rung.erase(gone[i]);
  }
}

std::string Brain::moodWord() const {
  float strength = 0.0f;
  const Axis axis = _weather.dominant(&strength);
  if (strength < 0.15f) {
    if (_mood > 0.3f) return "content";
    if (_mood < -0.3f) return "flat";
    return "fine";
  }
  switch (axis) {
    case kJoy:
      return "cheerful";
    case kTrust:
      return "settled";
    case kFear:
      return "uneasy";
    case kSurprise:
      return "startled";
    case kSadness:
      return "low";
    case kDisgust:
      return "fed up";
    case kAnger:
      return "irritated";
    case kAnticipation:
      return "expectant";
    default:
      return "fine";
  }
}

int Brain::feelingToward(const std::string& nick) const {
  std::map<std::string, int>::const_iterator it = _relations.find(nick);
  return it == _relations.end() ? 0 : it->second;
}

void Brain::adjust(const std::string& nick, int delta) {
  if (nick.empty()) return;
  int value = feelingToward(nick) + delta;
  if (value > 3) value = 3;
  if (value < -3) value = -3;
  _relations[nick] = value;
}

bool Brain::likes(const std::string& nick) const { return feelingToward(nick) >= 2; }
bool Brain::dislikes(const std::string& nick) const { return feelingToward(nick) <= -2; }

int Brain::rungFor(const std::string& speaker, float warnAt, float finalAt, float kickAt) const {
  const float total = pressureFrom(speaker);
  if (total >= kickAt) return 4;
  if (total >= finalAt) return 3;
  if (total >= warnAt) return 2;
  return 1;
}

int Brain::rungUsed(const std::string& speaker) const {
  std::map<std::string, int>::const_iterator it = _rung.find(speaker);
  return it == _rung.end() ? 0 : it->second;
}

void Brain::climbed(const std::string& speaker, int rung) {
  if (rung > rungUsed(speaker)) _rung[speaker] = rung;
}

float Brain::pressureFrom(const std::string& speaker) const {
  std::map<std::string, float>::const_iterator it = _pressure.find(speaker);
  return it == _pressure.end() ? 0.0f : it->second;
}

Thread& Brain::thread(const std::string& channel) {
  std::map<std::string, Thread>::iterator it = _threads.find(channel);
  if (it == _threads.end()) {
    Thread t;
    t.channel = channel;
    it = _threads.insert(std::make_pair(channel, t)).first;
  }
  return it->second;
}

const Thread* Brain::peekThread(const std::string& channel) const {
  std::map<std::string, Thread>::const_iterator it = _threads.find(channel);
  return it == _threads.end() ? NULL : &it->second;
}

float Brain::urgeToSpeak(const std::string& channel, float base, std::time_t now) const {
  const Thread* t = peekThread(channel);
  if (!t || t->lastSpeaker.empty()) return base * 0.5f;

  float score = base;

  //< Being spoken to is the strongest pull there is. Ignoring a direct
  //< address is what made the first version feel like scenery.
  if (t->addressee == _nick) score += 0.65f;

  //< An unanswered question is a hole in the conversation, and holes are
  //< what make a channel feel dead.
  if (t->questionOpen(now) && t->questionFrom != _nick) score += 0.35f;

  if (t->isLive(now))
    score += 0.15f;
  else if (t->lastAt && (now - t->lastAt) > 90)
    score += 0.10f;  //< a long silence wants a starter, but rarely

  if (t->lastSpeaker == _nick) score *= 0.15f;  //< do not answer myself
  if (now - _lastSpoke < 12) score *= 0.25f;    //< I just spoke here

  //< The rate limit that makes "denser" safe: past six messages in twenty
  //< seconds everybody backs off, so a lively thread cannot cascade into
  //< every bot in the channel shouting at once.
  const int busy = t->rate(now, 20);
  if (busy >= 6)
    score *= 0.15f;
  else if (busy >= 4)
    score *= 0.45f;

  if (score < 0.0f) score = 0.0f;
  if (score > 1.0f) score = 1.0f;
  return score;
}

bool Brain::onCooldown(std::time_t now, float seconds) const { return static_cast<float>(now - _lastSpoke) < seconds; }

void Brain::noteSpoke(std::time_t now) {
  _lastSpoke = now;
  ++_spoken;
}

// ── SimpleBrain ────────────────────────────────────────────────────────────

SimpleBrain::SimpleBrain(const std::string& nick, const Temperament& t, float sensitivity)
    : Brain(nick, t, sensitivity) {}

Brain::Reading SimpleBrain::read(const std::string& text, const std::string& speaker, const std::string& channel,
                                 std::time_t now) {
  //< Reads the words, then forgets everything about them: no weather, no
  //< pressure, no topic. Same voice, no memory.
  Reading r = Brain::read(text, speaker, channel, now);
  r.impact = 0.0f;
  return r;
}

}  // namespace Bots
