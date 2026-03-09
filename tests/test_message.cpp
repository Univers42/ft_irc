/* ─── Unit tests: Message::parse() ─── */

#include <gtest/gtest.h>
#include "Message.hpp"

/* ── Basic parsing ── */

TEST(MessageParser, SimpleCommand)
{
	Message msg = Message::parse("NICK foo");
	EXPECT_EQ(msg.command, "NICK");
	ASSERT_EQ(msg.params.size(), 1u);
	EXPECT_EQ(msg.params[0], "foo");
	EXPECT_TRUE(msg.prefix.empty());
}

TEST(MessageParser, CommandWithPrefix)
{
	Message msg = Message::parse(":server 001 nick :Welcome to IRC");
	EXPECT_EQ(msg.prefix, "server");
	EXPECT_EQ(msg.command, "001");
	ASSERT_GE(msg.params.size(), 2u);
	EXPECT_EQ(msg.params[0], "nick");
	EXPECT_EQ(msg.params[1], "Welcome to IRC");
}

TEST(MessageParser, TrailingParameter)
{
	Message msg = Message::parse(":nick!user@host PRIVMSG #chan :hello world");
	EXPECT_EQ(msg.prefix, "nick!user@host");
	EXPECT_EQ(msg.command, "PRIVMSG");
	ASSERT_EQ(msg.params.size(), 2u);
	EXPECT_EQ(msg.params[0], "#chan");
	EXPECT_EQ(msg.params[1], "hello world");
}

TEST(MessageParser, CommandUppercased)
{
	Message msg = Message::parse("nick foo");
	EXPECT_EQ(msg.command, "NICK");
}

TEST(MessageParser, EmptyInput)
{
	Message msg = Message::parse("");
	EXPECT_TRUE(msg.command.empty());
	EXPECT_TRUE(msg.params.empty());
	EXPECT_TRUE(msg.prefix.empty());
}

TEST(MessageParser, MultipleSpacesBetweenParams)
{
	Message msg = Message::parse("MODE   #channel   +o   nick");
	EXPECT_EQ(msg.command, "MODE");
	ASSERT_EQ(msg.params.size(), 3u);
	EXPECT_EQ(msg.params[0], "#channel");
	EXPECT_EQ(msg.params[1], "+o");
	EXPECT_EQ(msg.params[2], "nick");
}

TEST(MessageParser, CommandOnly)
{
	Message msg = Message::parse("QUIT");
	EXPECT_EQ(msg.command, "QUIT");
	EXPECT_TRUE(msg.params.empty());
}

TEST(MessageParser, PrefixOnly)
{
	Message msg = Message::parse(":onlyprefix");
	EXPECT_EQ(msg.prefix, "onlyprefix");
	EXPECT_TRUE(msg.command.empty());
}

TEST(MessageParser, MultipleMiddleAndTrailing)
{
	Message msg = Message::parse("USER guest 0 * :Real Name Here");
	EXPECT_EQ(msg.command, "USER");
	ASSERT_EQ(msg.params.size(), 4u);
	EXPECT_EQ(msg.params[0], "guest");
	EXPECT_EQ(msg.params[1], "0");
	EXPECT_EQ(msg.params[2], "*");
	EXPECT_EQ(msg.params[3], "Real Name Here");
}

TEST(MessageParser, TrailingEmpty)
{
	Message msg = Message::parse("TOPIC #chan :");
	EXPECT_EQ(msg.command, "TOPIC");
	ASSERT_EQ(msg.params.size(), 2u);
	EXPECT_EQ(msg.params[0], "#chan");
	EXPECT_EQ(msg.params[1], "");
}

TEST(MessageParser, LeadingWhitespace)
{
	Message msg = Message::parse("   PING server");
	EXPECT_EQ(msg.command, "PING");
	ASSERT_EQ(msg.params.size(), 1u);
	EXPECT_EQ(msg.params[0], "server");
}

TEST(MessageParser, PassCommand)
{
	Message msg = Message::parse("PASS secretpassword");
	EXPECT_EQ(msg.command, "PASS");
	ASSERT_EQ(msg.params.size(), 1u);
	EXPECT_EQ(msg.params[0], "secretpassword");
}

TEST(MessageParser, JoinWithKey)
{
	Message msg = Message::parse("JOIN #foo,#bar key1,key2");
	EXPECT_EQ(msg.command, "JOIN");
	ASSERT_EQ(msg.params.size(), 2u);
	EXPECT_EQ(msg.params[0], "#foo,#bar");
	EXPECT_EQ(msg.params[1], "key1,key2");
}

TEST(MessageParser, KickWithReason)
{
	Message msg = Message::parse(":op!u@h KICK #chan target :bad behavior");
	EXPECT_EQ(msg.prefix, "op!u@h");
	EXPECT_EQ(msg.command, "KICK");
	ASSERT_EQ(msg.params.size(), 3u);
	EXPECT_EQ(msg.params[0], "#chan");
	EXPECT_EQ(msg.params[1], "target");
	EXPECT_EQ(msg.params[2], "bad behavior");
}

TEST(MessageParser, ModeMultipleFlags)
{
	Message msg = Message::parse("MODE #chan +itk secret");
	EXPECT_EQ(msg.command, "MODE");
	ASSERT_EQ(msg.params.size(), 3u);
	EXPECT_EQ(msg.params[0], "#chan");
	EXPECT_EQ(msg.params[1], "+itk");
	EXPECT_EQ(msg.params[2], "secret");
}
