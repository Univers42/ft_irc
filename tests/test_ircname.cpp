/* ─── IrcName: the nickname / channel / key / username character rules ───
 *
 * These four predicates decide what the server will accept as a name. They
 * used to be const member functions on Server that never read a member, so
 * reaching them meant standing up a whole server -- and so, until this file,
 * none of them had a direct test. What coverage existed was incidental,
 * through the command handlers that call them.
 *
 * Each rule below is stated against RFC 2812 §2.3.1, and the cases that carry
 * a D-number are the ones a real bug went through:
 *
 *   D1  '`' (0x60) was excluded from the nick specials, so "z`tick" was
 *       rejected; the special ranges are %x5B-60 and %x7B-7D, nine characters.
 *   D4  "#a:b" is refused here -- stricter than RFC 2812, which reads a ':'
 *       in a channel name as a name:mask separator.
 *   D5  a key is 7-bit; "sécret" draws 525 rather than being silently mangled.
 */

#include <gtest/gtest.h>

#include <string>

#include "IrcName.hpp"
#include "Limits.hpp"

/* ── nickname ─────────────────────────────────────────────────────────── */

TEST(IrcNameNick, AcceptsPlainAndSpecialLeadCharacters)
{
	EXPECT_TRUE(IrcName::isNickname("alice"));
	EXPECT_TRUE(IrcName::isNickname("A"));
	EXPECT_TRUE(IrcName::isNickname("[bot]"));
	EXPECT_TRUE(IrcName::isNickname("z`tick")) << "D1: '`' is 0x60, inside the special range";
	for (char c = 0x5B; c <= 0x60; ++c)
		EXPECT_TRUE(IrcName::isNickname(std::string(1, c)))
			<< "%x5B-60 may lead a nickname, failing char: " << c;
	for (char c = 0x7B; c <= 0x7D; ++c)
		EXPECT_TRUE(IrcName::isNickname(std::string(1, c)))
			<< "%x7B-7D may lead a nickname, failing char: " << c;
}

TEST(IrcNameNick, RejectsALeadThatIsADigitOrDash)
{
	EXPECT_FALSE(IrcName::isNickname("1abc")) << "a nickname may not start with a digit";
	EXPECT_FALSE(IrcName::isNickname("-x")) << "'-' is a body character only";
	EXPECT_FALSE(IrcName::isNickname("#chan")) << "'#' is not a nickname character at all";
	EXPECT_FALSE(IrcName::isNickname("")) << "the empty string is not a nickname";
}

TEST(IrcNameNick, BodyAllowsDigitsAndDashButNotPunctuation)
{
	EXPECT_TRUE(IrcName::isNickname("a1"));
	EXPECT_TRUE(IrcName::isNickname("z-9"));
	EXPECT_TRUE(IrcName::isNickname("n|ck"));
	EXPECT_FALSE(IrcName::isNickname("a.b"));
	EXPECT_FALSE(IrcName::isNickname("a,b")) << "',' separates list parameters on the wire";
	EXPECT_FALSE(IrcName::isNickname("a b")) << "SPACE ends a parameter";
}

TEST(IrcNameNick, LengthBoundIsTheGrammarsOwnNicknameProduction)
{
	/* nickname = ( letter / special ) *8( letter / digit / special / "-" ),
	 * so nine characters is the most a nickname can hold. The boundary is
	 * asserted from Limits::kNickLen rather than a literal 9, so the two
	 * cannot drift apart. */
	EXPECT_TRUE(IrcName::isNickname(std::string(Limits::kNickLen, 'a')))
		<< "exactly NICKLEN must be accepted";
	EXPECT_FALSE(IrcName::isNickname(std::string(Limits::kNickLen + 1, 'a')))
		<< "one character over NICKLEN must be refused, not truncated";
}

/* ── channel name ─────────────────────────────────────────────────────── */

TEST(IrcNameChannel, RequiresAHashAndSomethingAfterIt)
{
	EXPECT_TRUE(IrcName::isChannelName("#c"));
	EXPECT_TRUE(IrcName::isChannelName("#general"));
	EXPECT_FALSE(IrcName::isChannelName("#")) << "a bare '#' has no chanstring after the prefix";
	EXPECT_FALSE(IrcName::isChannelName("general")) << "005 advertises CHANTYPES=#";
	EXPECT_FALSE(IrcName::isChannelName("&amp")) << "'&' is RFC-legal but not advertised here";
	EXPECT_FALSE(IrcName::isChannelName(""));
}

TEST(IrcNameChannel, RejectsCharactersThatWouldReframeTheLine)
{
	EXPECT_FALSE(IrcName::isChannelName("#a b")) << "SPACE ends a parameter";
	EXPECT_FALSE(IrcName::isChannelName("#a,b")) << "',' splits a JOIN list";
	EXPECT_FALSE(IrcName::isChannelName(std::string("#a\x07") + "b")) << "BEL is excluded by RFC 2812";
	EXPECT_FALSE(IrcName::isChannelName("#a:b")) << "D4: stricter than the RFC, which reads ':' as name:mask";
}

TEST(IrcNameChannel, HonoursTheAdvertisedChannelLengthBound)
{
	EXPECT_TRUE(IrcName::isChannelName("#" + std::string(Limits::kChannelLen - 1, 'c')));
	EXPECT_FALSE(IrcName::isChannelName("#" + std::string(Limits::kChannelLen, 'c')));
}

/* ── channel key ──────────────────────────────────────────────────────── */

TEST(IrcNameKey, IsOneToTwentyThreePrintableSevenBitOctets)
{
	EXPECT_TRUE(IrcName::isChannelKey("secret"));
	EXPECT_TRUE(IrcName::isChannelKey(std::string(Limits::kKeyLen, 'k')))
		<< "RFC key = 1*23( ... ), so exactly 23 fits";
	EXPECT_FALSE(IrcName::isChannelKey(std::string(Limits::kKeyLen + 1, 'k')));
	EXPECT_FALSE(IrcName::isChannelKey("")) << "a key has at least one octet";
}

TEST(IrcNameKey, RejectsSeparatorsControlBytesAndHighBytes)
{
	EXPECT_FALSE(IrcName::isChannelKey("se cret")) << "SPACE ends the parameter";
	EXPECT_FALSE(IrcName::isChannelKey("a,b")) << "',' splits JOIN's key list";
	EXPECT_FALSE(IrcName::isChannelKey(std::string("a\x01") + "b")) << "control bytes are excluded";
	EXPECT_FALSE(IrcName::isChannelKey(std::string("s\xC3\xA9" "cret")))
		<< "D5: a key is 7-bit, so a UTF-8 key draws 525 rather than being mangled";
}

/* ── username ─────────────────────────────────────────────────────────── */

TEST(IrcNameUser, AcceptsOrdinaryNamesAndRejectsTheFramingCharacters)
{
	EXPECT_TRUE(IrcName::isUsername("alice"));
	EXPECT_TRUE(IrcName::isUsername("a.b-c_d"));
	EXPECT_FALSE(IrcName::isUsername("")) << "a username has at least one octet";
	EXPECT_FALSE(IrcName::isUsername("a b")) << "SPACE (0x20) ends a parameter";
	EXPECT_FALSE(IrcName::isUsername(std::string("a\x00" "b", 3))) << "NUL is never legal";
	EXPECT_FALSE(IrcName::isUsername("a\rb")) << "CR would split the line";
	EXPECT_FALSE(IrcName::isUsername("a\nb")) << "LF would split the line";
	EXPECT_FALSE(IrcName::isUsername("a@b")) << "'@' separates user from host in a prefix";
}
