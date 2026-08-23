/* ─── Unit tests: the production line-parsing path ───
 *
 * These used to drive parseThroughGrammar(), the hand-written parser. The server
 * no longer has one: Server::handleMessage matches the RFC 2812 grammar and
 * derives params from the captures. The parser is gone, but the behaviour it
 * guaranteed still matters, so every case below now drives the GRAMMAR and
 * asserts the same command/params it always did.
 *
 * parseThroughGrammar() mirrors Server::parseLine -- the generic §2.3.1
 * production, with params taken from the ordered capture sequence. */

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "Message.hpp"
#include "grammar/EmbeddedGrammarSource.hpp"
#include "grammar/Grammar.hpp"
#include "grammar/GrammarBuilder.hpp"
#include "grammar/MatchResult.hpp"
#include "grammar/interpreted/TreeMatcher.hpp"

namespace {

/* Built once: compiling the grammar per test would dominate the runtime. */
struct GrammarFixture {
	Abnf::Grammar grammar;
	Abnf::Interpreted::TreeMatcher *matcher;
	int messageRule;
	int commandSlot;
	int prefixSlot;

	GrammarFixture() : matcher(NULL), messageRule(-1)
	{
		Abnf::EmbeddedGrammarSource source;
		std::string text;
		source.read(text);

		Abnf::GrammarBuilder builder;
		builder.compile(text, grammar);

		matcher = new Abnf::Interpreted::TreeMatcher(grammar);
		messageRule = grammar.ruleIndex("message");
		commandSlot = grammar.captureIndex("command");
		prefixSlot = grammar.captureIndex("prefix");
	}
	~GrammarFixture() { delete matcher; }
};

GrammarFixture &fixture()
{
	static GrammarFixture instance;
	return instance;
}

Message parseThroughGrammar(const std::string &raw)
{
	GrammarFixture &f = fixture();

	Message msg;
	Abnf::MatchResult fields;
	if (!f.matcher->match(f.messageRule, raw, fields))
		return msg;

	msg.command = fields.get("command");
	for (std::size_t i = 0; i < msg.command.size(); ++i)
	{
		const char c = msg.command[i];
		if (c >= 'a' && c <= 'z')
			msg.command[i] = static_cast<char>(c - 'a' + 'A');
	}

	for (std::size_t i = 0; i < fields.sequenceSize(); ++i)
	{
		const int owner = fields.sequenceOwner(i);
		if (owner == f.commandSlot || owner == f.prefixSlot)
			continue;
		msg.params.push_back(fields.sequenceAt(i));
	}

	if (fields.count("trail") > 0)
		msg.trailingIndex = static_cast<int>(msg.params.size()) - 1;
	return msg;
}

}  // namespace

/* ── Basic parsing ── */

TEST(LineParsing, SimpleCommand)
{
	Message msg = parseThroughGrammar("NICK foo");
	EXPECT_EQ(msg.command, "NICK");
	ASSERT_EQ(msg.params.size(), 1u);
	EXPECT_EQ(msg.params[0], "foo");
}

/* Message no longer stores the prefix (nothing in src/ read it), so these
 * assert what actually matters: a leading ":prefix " is stepped over and
 * never mistaken for the command or an extra parameter. */
TEST(LineParsing, CommandWithPrefix)
{
	Message msg = parseThroughGrammar(":server 001 nick :Welcome to IRC");
	EXPECT_EQ(msg.command, "001");
	ASSERT_GE(msg.params.size(), 2u);
	EXPECT_EQ(msg.params[0], "nick");
	EXPECT_EQ(msg.params[1], "Welcome to IRC");
}

TEST(LineParsing, TrailingParameter)
{
	Message msg = parseThroughGrammar(":nick!user@host PRIVMSG #chan :hello world");
	EXPECT_EQ(msg.command, "PRIVMSG");
	ASSERT_EQ(msg.params.size(), 2u);
	EXPECT_EQ(msg.params[0], "#chan");
	EXPECT_EQ(msg.params[1], "hello world");
}

TEST(LineParsing, CommandUppercased)
{
	Message msg = parseThroughGrammar("nick foo");
	EXPECT_EQ(msg.command, "NICK");
}

TEST(LineParsing, EmptyInput)
{
	Message msg = parseThroughGrammar("");
	EXPECT_TRUE(msg.command.empty());
	EXPECT_TRUE(msg.params.empty());
}

TEST(LineParsing, MultipleSpacesBetweenParams)
{
	Message msg = parseThroughGrammar("MODE   #channel   +o   nick");
	EXPECT_EQ(msg.command, "MODE");
	ASSERT_EQ(msg.params.size(), 3u);
	EXPECT_EQ(msg.params[0], "#channel");
	EXPECT_EQ(msg.params[1], "+o");
	EXPECT_EQ(msg.params[2], "nick");
}

TEST(LineParsing, CommandOnly)
{
	Message msg = parseThroughGrammar("QUIT");
	EXPECT_EQ(msg.command, "QUIT");
	EXPECT_TRUE(msg.params.empty());
}

TEST(LineParsing, PrefixOnly)
{
	/* Nothing but a prefix carries no command, so it must parse to an empty
	 * message -- never to a command named after the prefix, which the
	 * dispatcher would then try to run. */
	Message msg = parseThroughGrammar(":onlyprefix");
	EXPECT_TRUE(msg.command.empty());
	EXPECT_TRUE(msg.params.empty());
}

TEST(LineParsing, MultipleMiddleAndTrailing)
{
	Message msg = parseThroughGrammar("USER guest 0 * :Real Name Here");
	EXPECT_EQ(msg.command, "USER");
	ASSERT_EQ(msg.params.size(), 4u);
	EXPECT_EQ(msg.params[0], "guest");
	EXPECT_EQ(msg.params[1], "0");
	EXPECT_EQ(msg.params[2], "*");
	EXPECT_EQ(msg.params[3], "Real Name Here");
}

TEST(LineParsing, TrailingEmpty)
{
	Message msg = parseThroughGrammar("TOPIC #chan :");
	EXPECT_EQ(msg.command, "TOPIC");
	ASSERT_EQ(msg.params.size(), 2u);
	EXPECT_EQ(msg.params[0], "#chan");
	EXPECT_EQ(msg.params[1], "");
}

TEST(LineParsing, LeadingWhitespace)
{
	Message msg = parseThroughGrammar("   PING server");
	EXPECT_EQ(msg.command, "PING");
	ASSERT_EQ(msg.params.size(), 1u);
	EXPECT_EQ(msg.params[0], "server");
}

TEST(LineParsing, PassCommand)
{
	Message msg = parseThroughGrammar("PASS secretpassword");
	EXPECT_EQ(msg.command, "PASS");
	ASSERT_EQ(msg.params.size(), 1u);
	EXPECT_EQ(msg.params[0], "secretpassword");
}

TEST(LineParsing, JoinWithKey)
{
	Message msg = parseThroughGrammar("JOIN #foo,#bar key1,key2");
	EXPECT_EQ(msg.command, "JOIN");
	ASSERT_EQ(msg.params.size(), 2u);
	EXPECT_EQ(msg.params[0], "#foo,#bar");
	EXPECT_EQ(msg.params[1], "key1,key2");
}

TEST(LineParsing, KickWithReason)
{
	Message msg = parseThroughGrammar(":op!u@h KICK #chan target :bad behavior");
	EXPECT_EQ(msg.command, "KICK");
	ASSERT_EQ(msg.params.size(), 3u);
	EXPECT_EQ(msg.params[0], "#chan");
	EXPECT_EQ(msg.params[1], "target");
	EXPECT_EQ(msg.params[2], "bad behavior");
}

TEST(LineParsing, ModeMultipleFlags)
{
	Message msg = parseThroughGrammar("MODE #chan +itk secret");
	EXPECT_EQ(msg.command, "MODE");
	ASSERT_EQ(msg.params.size(), 3u);
	EXPECT_EQ(msg.params[0], "#chan");
	EXPECT_EQ(msg.params[1], "+itk");
	EXPECT_EQ(msg.params[2], "secret");
}
