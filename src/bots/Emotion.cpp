#include "bots/Emotion.hpp"

#include <cstring>

/*
** The arithmetic behind Plutchik's wheel. Nothing here reads text -- that is
** Lexicon's job; this file only knows how eight numbers combine.
*/
namespace Bots {

namespace {
const char* const kNames[kAxisCount] = {"joy",     "trust",   "fear",  "surprise",
                                        "sadness", "disgust", "anger", "anticipation"};

//< Plutchik's opposing pairs, in axis order.
const Axis kOpposite[kAxisCount] = {kSadness, kDisgust, kAnger, kAnticipation, kJoy, kTrust, kFear, kSurprise};
}  // namespace

const char* axisName(Axis axis) { return kNames[axis]; }

Axis opposite(Axis axis) { return kOpposite[axis]; }

Vector::Vector() { clear(); }

void Vector::clear() {
  for (int i = 0; i < kAxisCount; ++i) v[i] = 0.0f;
}

void Vector::scale(float factor) {
  for (int i = 0; i < kAxisCount; ++i) v[i] *= factor;
}

float Vector::hostility() const { return v[kAnger] * 1.0f + v[kDisgust] * 0.8f; }

float Vector::warmth() const { return v[kJoy] * 1.0f + v[kTrust] * 0.9f; }

float Vector::valence() const {
  const float good = warmth();
  const float bad = hostility() + v[kSadness] * 0.6f + v[kFear] * 0.4f;
  const float total = good + bad;
  if (total < 0.05f) return 0.0f;  //< nothing said either way
  float out = (good - bad) / total;
  if (out > 1.0f) out = 1.0f;
  if (out < -1.0f) out = -1.0f;
  return out;
}

Axis Vector::dominant(float* strength) const {
  Axis best = kJoy;
  float top = 0.0f;
  for (int i = 0; i < kAxisCount; ++i) {
    if (v[i] > top) {
      top = v[i];
      best = static_cast<Axis>(i);
    }
  }
  if (strength) *strength = top;
  return best;
}

void Vector::blend(const Vector& other, float weight) {
  for (int i = 0; i < kAxisCount; ++i) v[i] = v[i] * (1.0f - weight) + other.v[i] * weight;
}

Temperament::Temperament() {
  for (int i = 0; i < kAxisCount; ++i) m[i] = 1.0f;
}

Vector Temperament::apply(const Vector& in) const {
  Vector out;
  for (int i = 0; i < kAxisCount; ++i) out.v[i] = in.v[i] * m[i];
  return out;
}

namespace Temperaments {

Temperament neutral() { return Temperament(); }

//< HappyBot: kindness lands twice as hard, hostility barely registers.
Temperament joyful() {
  Temperament t;
  t[kJoy] = 1.9f;
  t[kTrust] = 1.6f;
  t[kAnger] = 0.5f;
  t[kSadness] = 0.6f;
  t[kDisgust] = 0.5f;
  return t;
}

//< SadBot: reads hostility as SADNESS rather than anger, and good news at
//< half strength -- which is why it is wounded where others are annoyed.
Temperament melancholic() {
  Temperament t;
  t[kSadness] = 2.2f;
  t[kFear] = 1.5f;
  t[kJoy] = 0.5f;
  t[kTrust] = 0.8f;
  t[kAnger] = 0.4f;
  return t;
}

//< GrumpyBot: the shortest fuse in the cast, ~6x JokerBot on one sentence.
Temperament irritable() {
  Temperament t;
  t[kAnger] = 2.4f;
  t[kDisgust] = 1.8f;
  t[kJoy] = 0.6f;
  t[kTrust] = 0.7f;
  t[kSurprise] = 0.6f;
  return t;
}

//< JokerBot: insults genuinely do not reach it, so it stays on rung 1.
Temperament flippant() {
  Temperament t;
  t[kAnger] = 0.3f;
  t[kDisgust] = 0.4f;
  t[kSadness] = 0.4f;
  t[kJoy] = 1.4f;
  t[kSurprise] = 1.5f;
  return t;
}

//< OverexcitedBot: everything is thrilling, including bad news.
Temperament manic() {
  Temperament t;
  t[kJoy] = 2.1f;
  t[kSurprise] = 2.0f;
  t[kAnticipation] = 1.8f;
  t[kSadness] = 0.5f;
  t[kAnger] = 0.6f;
  return t;
}

Temperament stoic() {
  Temperament t;
  t[kAnger] = 0.6f;
  t[kJoy] = 0.8f;
  t[kSadness] = 0.7f;
  t[kFear] = 0.6f;
  t[kTrust] = 1.2f;
  return t;
}

Temperament methodical() {
  Temperament t;
  t[kAnticipation] = 1.7f;
  t[kTrust] = 1.4f;
  t[kFear] = 1.2f;
  t[kJoy] = 0.8f;
  t[kAnger] = 0.6f;
  return t;
}

//< OperatorBot: fear and anticipation raised, so it reads trouble COMING
//< rather than only reacting once it has arrived.
Temperament vigilant() {
  Temperament t;
  t[kFear] = 1.6f;
  t[kAnger] = 1.3f;
  t[kAnticipation] = 1.5f;
  t[kTrust] = 1.2f;
  t[kJoy] = 0.8f;
  return t;
}

}  // namespace Temperaments

}  // namespace Bots
