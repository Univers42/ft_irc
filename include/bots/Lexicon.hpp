#ifndef BOTS_LEXICON_HPP
#define BOTS_LEXICON_HPP

#include <string>
#include <vector>

#include "bots/Emotion.hpp"

/*
** Reading text onto Plutchik's eight axes.
**
** The table is a static array searched by binary-free linear scan over a
** sorted block -- see Lexicon.cpp for why that is fast enough here and why a
** std::map would be worse. It is shared by every bot: they all READ a line
** identically and only disagree about what it FEELS like, which is the whole
** point of Temperament.
*/
namespace Bots {
namespace Lexicon {

//< Text -> emotion, before any bot's temperament. Handles negation (flips
//< onto the Plutchik opposite), intensity boosters/dampeners, ALL CAPS and
//< runs of '!'.
Vector read(const std::string& text);

//< Is this line aimed at `nick`? "nick:", "nick," or a word-boundary mention.
//< The boundary matters: without it CalmBot answered to "calm" inside "keep
//< calm", which read as interrupting at random.
bool addressedTo(const std::string& text, const std::string& nick);

//< Words worth treating as the channel's current subject. Stoplist-based, so
//< it can recognise a topic nobody thought of in advance.
std::vector<std::string> keywords(const std::string& text, std::size_t limit);

bool isQuestion(const std::string& text);
bool isFileRequest(const std::string& text);
bool isAppreciation(const std::string& text);
//< "you ok?", "hang in there" — kindness aimed at someone who is
//< struggling. Distinct from appreciation: thanking a bot and checking
//< on it deserve different answers.
bool isSympathy(const std::string& text);

//< The worst phrase found, for the log. An eight-axis vector is the right
//< thing to reason with and the wrong thing to print every line.
std::string worstPhrase(const std::string& text);

}  // namespace Lexicon
}  // namespace Bots

#endif
