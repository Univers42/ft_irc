#include "bots/Lexicon.hpp"

#include <string>
#include <vector>

#include <cctype>
#include <cstring>

/*
** The prefilled emotion table, hand-built.
**
** WHY A FLAT ARRAY AND NOT A std::map
** -----------------------------------
** The table is read-only and known at compile time. A std::map would cost a
** startup pass building nodes, a pointer chase per lookup, and an allocation
** per entry -- for a few hundred words scanned a handful of times per
** message. A flat static array is in one cache-friendly block, needs no
** construction, and makes the whole thing visible in one screen when tuning
** a bot that feels one-note. Lookup is a linear scan over that block, which
** at this size beats the map's constant factors.
**
** A word may load SEVERAL axes. "betrayed" is sadness and anger and broken
** trust at once, and reading it as only one of those is what flattens a
** character.
*/
namespace Bots {
namespace Lexicon {

namespace {

struct Entry {
  const char* word;
  Axis a1;
  float w1;
  Axis a2;
  float w2;
  Axis a3;
  float w3;
};

//< kAxisCount marks "no third axis" -- cheaper than a count field and it
//< cannot get out of step with the weights.
const Axis kNone = kAxisCount;

const Entry kTable[] = {
    /* -- anger ------------------------------------------------------- */
    {"stupid", kAnger, 0.7f, kDisgust, 0.4f, kNone, 0.0f},
    {"idiot", kAnger, 0.7f, kDisgust, 0.4f, kNone, 0.0f},
    {"moron", kAnger, 0.7f, kDisgust, 0.4f, kNone, 0.0f},
    {"imbecile", kAnger, 0.7f, kDisgust, 0.4f, kNone, 0.0f},
    {"dumb", kAnger, 0.7f, kDisgust, 0.4f, kNone, 0.0f},
    {"braindead", kAnger, 0.7f, kDisgust, 0.4f, kNone, 0.0f},
    {"clueless", kAnger, 0.6f, kDisgust, 0.3f, kNone, 0.0f},
    {"incompetent", kAnger, 0.6f, kDisgust, 0.4f, kNone, 0.0f},
    {"hate", kAnger, 0.95f, kNone, 0.0f, kNone, 0.0f},
    {"furious", kAnger, 0.95f, kNone, 0.0f, kNone, 0.0f},
    {"livid", kAnger, 0.95f, kNone, 0.0f, kNone, 0.0f},
    {"outraged", kAnger, 0.95f, kNone, 0.0f, kNone, 0.0f},
    {"rage", kAnger, 0.95f, kNone, 0.0f, kNone, 0.0f},
    {"annoying", kAnger, 0.6f, kNone, 0.0f, kNone, 0.0f},
    {"irritating", kAnger, 0.6f, kNone, 0.0f, kNone, 0.0f},
    {"infuriating", kAnger, 0.7f, kNone, 0.0f, kNone, 0.0f},
    {"insufferable", kAnger, 0.7f, kDisgust, 0.3f, kNone, 0.0f},
    {"shut", kAnger, 0.35f, kNone, 0.0f, kNone, 0.0f},
    {"useless", kAnger, 0.5f, kDisgust, 0.5f, kSadness, 0.2f},
    {"worthless", kAnger, 0.5f, kDisgust, 0.5f, kSadness, 0.2f},
    {"pathetic", kAnger, 0.5f, kDisgust, 0.5f, kSadness, 0.2f},
    {"hopeless", kAnger, 0.4f, kSadness, 0.5f, kNone, 0.0f},
    {"lazy", kAnger, 0.5f, kDisgust, 0.4f, kNone, 0.0f},
    {"sloppy", kAnger, 0.4f, kDisgust, 0.4f, kNone, 0.0f},
    /* -- disgust ----------------------------------------------------- */
    {"disgusting", kDisgust, 0.9f, kAnger, 0.3f, kNone, 0.0f},
    {"revolting", kDisgust, 0.9f, kAnger, 0.3f, kNone, 0.0f},
    {"vile", kDisgust, 0.9f, kAnger, 0.3f, kNone, 0.0f},
    {"repulsive", kDisgust, 0.9f, kAnger, 0.3f, kNone, 0.0f},
    {"gross", kDisgust, 0.8f, kNone, 0.0f, kNone, 0.0f},
    {"nasty", kDisgust, 0.8f, kAnger, 0.3f, kNone, 0.0f},
    {"garbage", kDisgust, 0.6f, kAnger, 0.3f, kSadness, 0.2f},
    {"trash", kDisgust, 0.6f, kAnger, 0.3f, kSadness, 0.2f},
    {"rubbish", kDisgust, 0.6f, kAnger, 0.3f, kNone, 0.0f},
    {"crap", kDisgust, 0.6f, kAnger, 0.3f, kNone, 0.0f},
    {"awful", kDisgust, 0.6f, kSadness, 0.3f, kNone, 0.0f},
    {"terrible", kDisgust, 0.6f, kSadness, 0.3f, kNone, 0.0f},
    {"horrible", kDisgust, 0.6f, kSadness, 0.3f, kNone, 0.0f},
    /* -- sadness ----------------------------------------------------- */
    {"sad", kSadness, 0.9f, kNone, 0.0f, kNone, 0.0f},
    {"unhappy", kSadness, 0.9f, kNone, 0.0f, kNone, 0.0f},
    {"miserable", kSadness, 0.9f, kNone, 0.0f, kNone, 0.0f},
    {"depressed", kSadness, 0.9f, kNone, 0.0f, kNone, 0.0f},
    {"hurt", kSadness, 0.9f, kNone, 0.0f, kNone, 0.0f},
    {"crying", kSadness, 0.9f, kNone, 0.0f, kNone, 0.0f},
    {"grief", kSadness, 0.9f, kNone, 0.0f, kNone, 0.0f},
    {"alone", kSadness, 0.6f, kFear, 0.2f, kNone, 0.0f},
    {"lonely", kSadness, 0.6f, kFear, 0.2f, kNone, 0.0f},
    {"ignored", kSadness, 0.6f, kFear, 0.2f, kNone, 0.0f},
    {"forgotten", kSadness, 0.6f, kFear, 0.2f, kNone, 0.0f},
    {"nobody", kSadness, 0.5f, kNone, 0.0f, kNone, 0.0f},
    {"sorry", kSadness, 0.5f, kTrust, 0.2f, kNone, 0.0f},
    {"regret", kSadness, 0.5f, kNone, 0.0f, kNone, 0.0f},
    {"mistake", kSadness, 0.4f, kNone, 0.0f, kNone, 0.0f},
    {"failed", kSadness, 0.5f, kNone, 0.0f, kNone, 0.0f},
    {"failure", kSadness, 0.5f, kNone, 0.0f, kNone, 0.0f},
    {"broken", kSadness, 0.5f, kFear, 0.2f, kNone, 0.0f},
    {"betrayed", kSadness, 0.7f, kAnger, 0.6f, kDisgust, 0.5f},
    {"lied", kSadness, 0.5f, kAnger, 0.5f, kDisgust, 0.4f},
    /* -- fear -------------------------------------------------------- */
    {"afraid", kFear, 0.9f, kNone, 0.0f, kNone, 0.0f},
    {"scared", kFear, 0.9f, kNone, 0.0f, kNone, 0.0f},
    {"terrified", kFear, 0.95f, kNone, 0.0f, kNone, 0.0f},
    {"worried", kFear, 0.8f, kNone, 0.0f, kNone, 0.0f},
    {"anxious", kFear, 0.8f, kNone, 0.0f, kNone, 0.0f},
    {"nervous", kFear, 0.7f, kNone, 0.0f, kNone, 0.0f},
    {"panic", kFear, 0.9f, kSurprise, 0.3f, kNone, 0.0f},
    {"dangerous", kFear, 0.6f, kAnticipation, 0.3f, kNone, 0.0f},
    {"risky", kFear, 0.6f, kAnticipation, 0.3f, kNone, 0.0f},
    {"crash", kFear, 0.6f, kAnticipation, 0.3f, kNone, 0.0f},
    {"crashed", kFear, 0.6f, kSurprise, 0.3f, kNone, 0.0f},
    {"segfault", kFear, 0.6f, kSurprise, 0.3f, kNone, 0.0f},
    {"leak", kFear, 0.6f, kAnticipation, 0.2f, kNone, 0.0f},
    {"deadlock", kFear, 0.6f, kAnticipation, 0.2f, kNone, 0.0f},
    {"outage", kFear, 0.7f, kSurprise, 0.3f, kNone, 0.0f},
    {"warning", kFear, 0.4f, kAnticipation, 0.3f, kNone, 0.0f},
    {"careful", kFear, 0.4f, kAnticipation, 0.3f, kNone, 0.0f},
    /* -- joy --------------------------------------------------------- */
    {"happy", kJoy, 0.9f, kTrust, 0.2f, kNone, 0.0f},
    {"glad", kJoy, 0.9f, kTrust, 0.2f, kNone, 0.0f},
    {"delighted", kJoy, 0.9f, kTrust, 0.2f, kNone, 0.0f},
    {"thrilled", kJoy, 0.95f, kSurprise, 0.3f, kNone, 0.0f},
    {"wonderful", kJoy, 0.9f, kTrust, 0.2f, kNone, 0.0f},
    {"amazing", kJoy, 0.9f, kSurprise, 0.3f, kNone, 0.0f},
    {"fantastic", kJoy, 0.9f, kSurprise, 0.2f, kNone, 0.0f},
    {"brilliant", kJoy, 0.9f, kTrust, 0.3f, kNone, 0.0f},
    {"excellent", kJoy, 0.9f, kTrust, 0.3f, kNone, 0.0f},
    {"great", kJoy, 0.8f, kTrust, 0.2f, kNone, 0.0f},
    {"awesome", kJoy, 0.9f, kSurprise, 0.2f, kNone, 0.0f},
    {"perfect", kJoy, 0.9f, kTrust, 0.3f, kNone, 0.0f},
    {"thanks", kJoy, 0.6f, kTrust, 0.7f, kNone, 0.0f},
    {"thank", kJoy, 0.6f, kTrust, 0.7f, kNone, 0.0f},
    {"cheers", kJoy, 0.5f, kTrust, 0.6f, kNone, 0.0f},
    {"appreciate", kJoy, 0.6f, kTrust, 0.7f, kNone, 0.0f},
    {"grateful", kJoy, 0.6f, kTrust, 0.7f, kNone, 0.0f},
    {"works", kJoy, 0.6f, kTrust, 0.4f, kAnticipation, 0.2f},
    {"working", kJoy, 0.5f, kTrust, 0.4f, kNone, 0.0f},
    {"fixed", kJoy, 0.6f, kTrust, 0.4f, kNone, 0.0f},
    {"solved", kJoy, 0.6f, kTrust, 0.4f, kNone, 0.0f},
    {"passed", kJoy, 0.6f, kTrust, 0.4f, kNone, 0.0f},
    {"shipped", kJoy, 0.6f, kTrust, 0.3f, kNone, 0.0f},
    {"merged", kJoy, 0.5f, kTrust, 0.3f, kNone, 0.0f},
    {"lol", kJoy, 0.7f, kSurprise, 0.2f, kNone, 0.0f},
    {"haha", kJoy, 0.7f, kSurprise, 0.2f, kNone, 0.0f},
    {"funny", kJoy, 0.7f, kSurprise, 0.2f, kNone, 0.0f},
    {"hilarious", kJoy, 0.8f, kSurprise, 0.3f, kNone, 0.0f},
    /* -- trust ------------------------------------------------------- */
    {"agree", kTrust, 0.7f, kNone, 0.0f, kNone, 0.0f},
    {"agreed", kTrust, 0.7f, kNone, 0.0f, kNone, 0.0f},
    {"correct", kTrust, 0.7f, kNone, 0.0f, kNone, 0.0f},
    {"exactly", kTrust, 0.7f, kJoy, 0.2f, kNone, 0.0f},
    {"reliable", kTrust, 0.8f, kNone, 0.0f, kNone, 0.0f},
    {"solid", kTrust, 0.7f, kNone, 0.0f, kNone, 0.0f},
    {"stable", kTrust, 0.7f, kNone, 0.0f, kNone, 0.0f},
    {"safe", kTrust, 0.7f, kNone, 0.0f, kNone, 0.0f},
    {"help", kTrust, 0.6f, kJoy, 0.3f, kNone, 0.0f},
    {"helpful", kTrust, 0.6f, kJoy, 0.3f, kNone, 0.0f},
    {"welcome", kTrust, 0.6f, kJoy, 0.4f, kNone, 0.0f},
    {"friend", kTrust, 0.7f, kJoy, 0.3f, kNone, 0.0f},
    {"please", kTrust, 0.3f, kNone, 0.0f, kNone, 0.0f},
    /* -- surprise ---------------------------------------------------- */
    {"wow", kSurprise, 0.8f, kNone, 0.0f, kNone, 0.0f},
    {"whoa", kSurprise, 0.8f, kNone, 0.0f, kNone, 0.0f},
    {"unbelievable", kSurprise, 0.8f, kNone, 0.0f, kNone, 0.0f},
    {"shocking", kSurprise, 0.8f, kFear, 0.3f, kNone, 0.0f},
    {"suddenly", kSurprise, 0.7f, kNone, 0.0f, kNone, 0.0f},
    {"unexpected", kSurprise, 0.8f, kNone, 0.0f, kNone, 0.0f},
    {"seriously", kSurprise, 0.6f, kNone, 0.0f, kNone, 0.0f},
    {"weird", kSurprise, 0.6f, kFear, 0.2f, kNone, 0.0f},
    {"strange", kSurprise, 0.6f, kFear, 0.2f, kNone, 0.0f},
    {"bizarre", kSurprise, 0.7f, kFear, 0.2f, kNone, 0.0f},
    /* -- anticipation ------------------------------------------------ */
    {"soon", kAnticipation, 0.7f, kNone, 0.0f, kNone, 0.0f},
    {"tomorrow", kAnticipation, 0.6f, kNone, 0.0f, kNone, 0.0f},
    {"waiting", kAnticipation, 0.7f, kNone, 0.0f, kNone, 0.0f},
    {"ready", kAnticipation, 0.7f, kTrust, 0.3f, kNone, 0.0f},
    {"almost", kAnticipation, 0.7f, kNone, 0.0f, kNone, 0.0f},
    {"review", kAnticipation, 0.5f, kTrust, 0.2f, kNone, 0.0f},
    {"testing", kAnticipation, 0.5f, kTrust, 0.2f, kNone, 0.0f},
    {"deploy", kAnticipation, 0.6f, kFear, 0.2f, kNone, 0.0f},
    {"release", kAnticipation, 0.6f, kNone, 0.0f, kNone, 0.0f},
    {"build", kAnticipation, 0.5f, kNone, 0.0f, kNone, 0.0f},
    {"merge", kAnticipation, 0.5f, kNone, 0.0f, kNone, 0.0f},
    {NULL, kNone, 0.0f, kNone, 0.0f, kNone, 0.0f},
};

struct Modifier {
  const char* word;
  float factor;
};

//< Applied to the WHOLE vector, not one axis: "really" makes any feeling
//< stronger and "slightly" makes any feeling weaker.
const Modifier kModifiers[] = {
    {"very", 1.5f},       {"really", 1.5f},     {"so", 1.3f},       {"extremely", 1.9f},  {"totally", 1.6f},
    {"absolutely", 1.8f}, {"completely", 1.7f}, {"utterly", 1.9f},  {"incredibly", 1.8f}, {"damn", 1.4f},
    {"always", 1.4f},     {"slightly", 0.5f},   {"somewhat", 0.6f}, {"kinda", 0.7f},      {"maybe", 0.6f},
    {"perhaps", 0.6f},    {NULL, 0.0f},
};

const char* const kNegators[] = {"not",    "no",   "never", "isnt",    "arent", "dont",
                                 "doesnt", "cant", "wont",  "nothing", NULL};

const char* const kStopwords[] = {"the",   "and",  "for",   "you",  "are",   "was",  "were",  "this", "that", "with",
                                  "have",  "has",  "had",   "but",  "not",   "all",  "any",   "can",  "will", "just",
                                  "get",   "got",  "how",   "why",  "what",  "when", "where", "who",  "its",  "our",
                                  "their", "his",  "her",   "them", "they",  "from", "into",  "over", "some", "such",
                                  "than",  "then", "there", "here", "about", NULL};

const char* const kFileWords[] = {"file", "files", "upload", "download", "archive", "index",
                                  "log",  "logs",  "dump",   "trace",    NULL};

const char* const kAppreciation[] = {"thanks", "thank", "appreciate", "grateful", "cheers", "agreed", "exactly", NULL};

std::string lower(const std::string& s) {
  std::string out(s);
  for (std::string::size_type i = 0; i < out.size(); ++i)
    out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[i])));
  return out;
}

//< Strip punctuation so "stupid," matches "stupid". Kept to the characters a
//< sentence actually ends with -- '-' and '_' stay, because they are inside
//< identifiers people type in a dev channel.
std::string strip(const std::string& w) {
  std::string::size_type b = 0, e = w.size();
  while (b < e && std::ispunct(static_cast<unsigned char>(w[b])) && w[b] != '#') ++b;
  while (e > b && std::ispunct(static_cast<unsigned char>(w[e - 1]))) --e;
  return w.substr(b, e - b);
}

std::vector<std::string> tokenise(const std::string& text) {
  std::vector<std::string> out;
  std::string cur;
  for (std::string::size_type i = 0; i < text.size(); ++i) {
    if (std::isspace(static_cast<unsigned char>(text[i]))) {
      if (!cur.empty()) out.push_back(strip(cur));
      cur.clear();
    } else {
      cur += static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
    }
  }
  if (!cur.empty()) out.push_back(strip(cur));
  return out;
}

bool inList(const char* const* list, const std::string& word) {
  for (int i = 0; list[i]; ++i)
    if (word == list[i]) return true;
  return false;
}

const Entry* lookup(const std::string& word) {
  for (int i = 0; kTable[i].word; ++i)
    if (word == kTable[i].word) return &kTable[i];
  return NULL;
}

bool isWordChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '[' || c == ']' || c == '{' ||
         c == '}' || c == '\\' || c == '|' || c == '^' || c == '`';
}

}  // namespace

Vector read(const std::string& text) {
  Vector vec;
  const std::vector<std::string> words = tokenise(text);

  float multiplier = 1.0f;
  bool negate = false;

  for (std::vector<std::string>::size_type i = 0; i < words.size(); ++i) {
    const std::string& w = words[i];
    if (w.empty()) continue;

    bool wasModifier = false;
    for (int m = 0; kModifiers[m].word; ++m) {
      if (w == kModifiers[m].word) {
        multiplier = kModifiers[m].factor;
        wasModifier = true;
        break;
      }
    }
    if (wasModifier) continue;

    if (inList(kNegators, w)) {
      negate = true;
      continue;
    }

    const Entry* e = lookup(w);
    if (!e) continue;

    const Axis axes[3] = {e->a1, e->a2, e->a3};
    const float weights[3] = {e->w1, e->w2, e->w3};
    for (int k = 0; k < 3; ++k) {
      if (axes[k] == kNone) continue;
      const float value = weights[k] * multiplier;
      if (negate) {
        //< "not happy" is SADNESS, not an absence of feeling. Treating a
        //< negation as zero is how a sarcastic channel reads as neutral.
        vec[opposite(axes[k])] += (value < 0 ? -value : value) * 0.8f;
      } else {
        vec[axes[k]] += value;
      }
    }
    multiplier = 1.0f;
    negate = false;
  }

  //< Contradictory pairs usually mean sarcasm: register as ambiguity rather
  //< than as two strong feelings at once.
  for (int i = 0; i < kAxisCount; ++i) {
    const Axis a = static_cast<Axis>(i);
    const Axis o = opposite(a);
    const float both = vec[a] < vec[o] ? vec[a] : vec[o];
    if (both > 0.0f) {
      vec[a] -= both * 0.5f;
      vec[o] -= both * 0.5f;
    }
  }

  //< Shouting is intensity.
  std::size_t letters = 0, upper = 0, bangs = 0;
  for (std::string::size_type i = 0; i < text.size(); ++i) {
    if (std::isalpha(static_cast<unsigned char>(text[i]))) {
      ++letters;
      if (std::isupper(static_cast<unsigned char>(text[i]))) ++upper;
    } else if (text[i] == '!') {
      ++bangs;
    }
  }
  if (letters >= 4 && static_cast<float>(upper) / static_cast<float>(letters) > 0.7f) vec.scale(1.4f);
  if (bangs) {
    float boost = 1.0f + static_cast<float>(bangs) * 0.15f;
    if (boost > 1.5f) boost = 1.5f;
    vec.scale(boost);
  }

  for (int i = 0; i < kAxisCount; ++i) {
    const Axis a = static_cast<Axis>(i);
    if (vec[a] < 0.0f) vec[a] = 0.0f;
    if (vec[a] > 3.0f) vec[a] = 3.0f;
  }
  return vec;
}

bool addressedTo(const std::string& text, const std::string& nick) {
  if (nick.empty()) return false;
  const std::string low = lower(text);
  const std::string n = lower(nick);

  //< "nick:" / "nick," at the start is explicit address.
  std::string::size_type i = 0;
  while (i < low.size() && std::isspace(static_cast<unsigned char>(low[i]))) ++i;
  if (low.compare(i, n.size(), n) == 0) {
    std::string::size_type after = i + n.size();
    while (after < low.size() && std::isspace(static_cast<unsigned char>(low[after]))) ++after;
    if (after < low.size() && (low[after] == ':' || low[after] == ',')) return true;
  }

  //< Otherwise a mention, but only on a word boundary.
  std::string::size_type at = low.find(n);
  while (at != std::string::npos) {
    const bool leftOk = (at == 0) || !isWordChar(low[at - 1]);
    const std::string::size_type end = at + n.size();
    const bool rightOk = (end >= low.size()) || !isWordChar(low[end]);
    if (leftOk && rightOk) return true;
    at = low.find(n, at + 1);
  }
  return false;
}

std::vector<std::string> keywords(const std::string& text, std::size_t limit) {
  std::vector<std::string> out;
  const std::vector<std::string> words = tokenise(text);
  for (std::vector<std::string>::size_type i = 0; i < words.size() && out.size() < limit; ++i) {
    const std::string& w = words[i];
    if (w.size() < 3 || inList(kStopwords, w)) continue;
    if (!std::isalpha(static_cast<unsigned char>(w[0]))) continue;
    bool seen = false;
    for (std::vector<std::string>::size_type j = 0; j < out.size(); ++j)
      if (out[j] == w) seen = true;
    if (!seen) out.push_back(w);
  }
  return out;
}

bool isQuestion(const std::string& text) {
  for (std::string::size_type i = text.size(); i > 0; --i) {
    const char c = text[i - 1];
    if (std::isspace(static_cast<unsigned char>(c))) continue;
    return c == '?';
  }
  return false;
}

bool isFileRequest(const std::string& text) {
  const std::vector<std::string> words = tokenise(text);
  for (std::vector<std::string>::size_type i = 0; i < words.size(); ++i)
    if (inList(kFileWords, words[i])) return true;
  return false;
}

bool isAppreciation(const std::string& text) {
  const std::vector<std::string> words = tokenise(text);
  for (std::vector<std::string>::size_type i = 0; i < words.size(); ++i)
    if (inList(kAppreciation, words[i])) return true;
  return false;
}

std::string worstPhrase(const std::string& text) {
  const std::vector<std::string> words = tokenise(text);
  std::string worst;
  float top = 0.0f;
  for (std::vector<std::string>::size_type i = 0; i < words.size(); ++i) {
    const Entry* e = lookup(words[i]);
    if (!e) continue;
    Vector v;
    const Axis axes[3] = {e->a1, e->a2, e->a3};
    const float weights[3] = {e->w1, e->w2, e->w3};
    for (int k = 0; k < 3; ++k)
      if (axes[k] != kNone) v[axes[k]] += weights[k];
    const float h = v.hostility();
    if (h > top) {
      top = h;
      worst = words[i];
    }
  }
  return worst;
}

}  // namespace Lexicon
}  // namespace Bots
