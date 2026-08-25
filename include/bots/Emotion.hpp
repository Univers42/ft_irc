#ifndef BOTS_EMOTION_HPP
#define BOTS_EMOTION_HPP

#include <cstddef>
#include <string>

/*
** Plutchik's eight primary emotions, as a value type.
**
** The same model the Python ecosystem uses (scripts/sim/bot/emotion.py) and
** the same one the NRC Emotion Lexicon is built on. No third-party table: a
** 42 project ships without dependencies, and a lexicon we own is one we can
** read and tune. Bots::Lexicon holds ours.
**
** WHY A VECTOR RATHER THAN A SCORE
** --------------------------------
** Grading a line on one axis -- "how rude" -- makes every bot read every
** message identically, and personality can then only change the words that
** come back. "You idiot", "I'm scared this will break" and "this is
** disgusting" are three different feelings; a cast that cannot tell them
** apart is one character in eight costumes.
**
** THE PART THAT MAKES THE CAST
** ----------------------------
** Temperament is a second vector of MULTIPLIERS, one per bot. The lexicon
** says what a line contains; the temperament says what this bot feels about
** it. GrumpyBot multiplies anger by 2.4 and JokerBot by 0.3, so one sentence
** genuinely lands as fury on one and as noise on the other -- without a
** single per-bot branch anywhere in the decision code.
*/
namespace Bots {

enum Axis { kJoy = 0, kTrust, kFear, kSurprise, kSadness, kDisgust, kAnger, kAnticipation, kAxisCount };

const char* axisName(Axis axis);

//< Plutchik pairs each emotion with its opposite. Used twice: to flip a
//< negated word onto the far side ("not happy" is sadness, not an absence),
//< and to damp contradictory readings, which usually mean sarcasm.
Axis opposite(Axis axis);

/*
** Eight floats. Deliberately a plain value type -- copied freely, no
** allocation, no virtuals; a bot reads dozens of these per second.
*/
struct Vector {
  float v[kAxisCount];

  Vector();

  float& operator[](Axis a) { return v[a]; }
  const float& operator[](Axis a) const { return v[a]; }

  void clear();
  void scale(float factor);

  //< Anger plus disgust: the axes that make somebody reach for a kick.
  //< Sadness and fear are unpleasant but not confrontational, and conflating
  //< them is how a bot ends up kicking a person for being upset.
  float hostility() const;

  //< Joy plus trust.
  float warmth() const;

  //< -1..1, for nudging mood.
  float valence() const;

  //< The strongest axis, and how strong. `strength` may be NULL.
  Axis dominant(float* strength) const;

  //< Exponential blend, for a bot's slow-moving emotional weather.
  void blend(const Vector& other, float weight);
};

/*
** One bot's own dictionary over the same eight axes. 1.0 means "feels this
** normally"; below is thick skin, above is a raw nerve.
*/
struct Temperament {
  float m[kAxisCount];

  Temperament();  //< all 1.0

  float& operator[](Axis a) { return m[a]; }
  const float& operator[](Axis a) const { return m[a]; }

  Vector apply(const Vector& in) const;
};

//< The cast's temperaments. Named rather than built inline so a bot's
//< declaration reads as a character sheet.
namespace Temperaments {
Temperament neutral();
Temperament joyful();       //< HappyBot
Temperament melancholic();  //< SadBot
Temperament irritable();    //< GrumpyBot
Temperament flippant();     //< JokerBot
Temperament manic();        //< OverexcitedBot
Temperament stoic();        //< CalmBot
Temperament methodical();   //< FileBot
Temperament vigilant();     //< OperatorBot
}  // namespace Temperaments

}  // namespace Bots

#endif
