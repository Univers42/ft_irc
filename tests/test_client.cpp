/* ─── Unit tests: Client buffer management & state ─── */

#include <gtest/gtest.h>
#include "PostMan.hpp"
#include "Client.hpp"
#include "Replies.hpp"

/* ════════════════════════════════════════════════════════════════════════
 * Suite: ClientBuffer — extractMessages() & send buffer
 * ════════════════════════════════════════════════════════════════════ */

TEST(ClientBuffer, ExtractWithCRLF)
{
	Client c(42, "127.0.0.1");
	c.appendToRecvBuffer("NICK foo\r\nUSER bar 0 * :baz\r\n");
	std::vector<std::string> msgs = c.extractMessages();
	ASSERT_EQ(msgs.size(), 2u);
	EXPECT_EQ(msgs[0], "NICK foo");
	EXPECT_EQ(msgs[1], "USER bar 0 * :baz");
}

TEST(ClientBuffer, ExtractWithBareNewline)
{
	Client c(43, "127.0.0.1");
	c.appendToRecvBuffer("NICK foo\nUSER bar 0 * :baz\n");
	std::vector<std::string> msgs = c.extractMessages();
	ASSERT_EQ(msgs.size(), 2u);
	EXPECT_EQ(msgs[0], "NICK foo");
	EXPECT_EQ(msgs[1], "USER bar 0 * :baz");
}

TEST(ClientBuffer, PartialMessagePreserved)
{
	Client c(44, "127.0.0.1");
	c.appendToRecvBuffer("NICK fo");
	std::vector<std::string> msgs = c.extractMessages();
	EXPECT_TRUE(msgs.empty());

	/* Complete it */
	c.appendToRecvBuffer("o\r\n");
	msgs = c.extractMessages();
	ASSERT_EQ(msgs.size(), 1u);
	EXPECT_EQ(msgs[0], "NICK foo");
}

TEST(ClientBuffer, MultipleMessagesInOneRecv)
{
	Client c(45, "127.0.0.1");
	c.appendToRecvBuffer("A\r\nB\r\nC\r\n");
	std::vector<std::string> msgs = c.extractMessages();
	ASSERT_EQ(msgs.size(), 3u);
	EXPECT_EQ(msgs[0], "A");
	EXPECT_EQ(msgs[1], "B");
	EXPECT_EQ(msgs[2], "C");
}

TEST(ClientBuffer, OverflowProtection)
{
	Client c(46, "127.0.0.1");
	/* Build a string > MAX_MSGLEN (512) with no newline */
	std::string huge(600, 'X');
	c.appendToRecvBuffer(huge);
	std::vector<std::string> msgs = c.extractMessages();
	/* Should force-extract at MAX_MSGLEN */
	ASSERT_EQ(msgs.size(), 1u);
	EXPECT_EQ(msgs[0].size(), static_cast<size_t>(MAX_MSGLEN));
}

TEST(ClientBuffer, QueueMessageAppendsCRLF)
{
	Client c(47, "127.0.0.1");
	c.queueMessage("PONG :server");
	EXPECT_EQ(c.getSendBuffer(), "PONG :server\r\n");
}

TEST(ClientBuffer, ClearSendBufferPartial)
{
	Client c(48, "127.0.0.1");
	c.queueMessage("Hello");
	c.queueMessage("World");
	/* Buffer: "Hello\r\nWorld\r\n" = 14 bytes */
	EXPECT_EQ(c.getSendBuffer().size(), 14u);
	c.clearSendBuffer(7); /* Remove "Hello\r\n" */
	EXPECT_EQ(c.getSendBuffer(), "World\r\n");
}

TEST(ClientBuffer, HasPendingData)
{
	Client c(49, "127.0.0.1");
	EXPECT_FALSE(c.hasPendingData());
	c.queueMessage("test");
	EXPECT_TRUE(c.hasPendingData());
	c.clearSendBuffer(c.getSendBuffer().size());
	EXPECT_FALSE(c.hasPendingData());
}

TEST(ClientBuffer, EmptyLinesSkipped)
{
	Client c(50, "127.0.0.1");
	c.appendToRecvBuffer("\r\n\r\nNICK foo\r\n\r\n");
	std::vector<std::string> msgs = c.extractMessages();
	ASSERT_EQ(msgs.size(), 1u);
	EXPECT_EQ(msgs[0], "NICK foo");
}

TEST(ClientBuffer, MixedCRLFAndLF)
{
	/* Every '\n' is a line boundary, with an optional preceding '\r'
	   stripped — mixed CRLF/LF peers can never produce a "line" with an
	   embedded newline. "A\r\nB\nC\r\n" → "A", "B", "C". */
	Client c(51, "127.0.0.1");
	c.appendToRecvBuffer("A\r\nB\nC\r\n");
	std::vector<std::string> msgs = c.extractMessages();
	ASSERT_EQ(msgs.size(), 3u);
	EXPECT_EQ(msgs[0], "A");
	EXPECT_EQ(msgs[1], "B");
	EXPECT_EQ(msgs[2], "C");
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: ClientState — initial state, prefix, flags
 * ════════════════════════════════════════════════════════════════════ */

TEST(ClientState, InitialState)
{
	Client c(100, "192.168.1.1");
	EXPECT_EQ(c.getFd(), 100);
	EXPECT_EQ(c.getNickname(), "*");
	EXPECT_EQ(c.getHostname(), "192.168.1.1");
	EXPECT_FALSE(c.isAuthenticated());
	EXPECT_FALSE(c.isRegistered());
	EXPECT_FALSE(c.hasPassSent());
	EXPECT_FALSE(c.hasNick());
	EXPECT_FALSE(c.hasUser());
	EXPECT_FALSE(c.isPingSent());
	EXPECT_FALSE(c.hasPendingData());
}

TEST(ClientState, GetPrefix)
{
	Client c(101, "10.0.0.1");
	c.setNickname("alice");
	c.setUsername("auser");
	EXPECT_EQ(c.getPrefix(), "alice!auser@10.0.0.1");
}

TEST(ClientState, FlagToggles)
{
	Client c(102, "127.0.0.1");
	c.setPassSent(true);
	EXPECT_TRUE(c.hasPassSent());
	c.setNickSet(true);
	EXPECT_TRUE(c.hasNick());
	c.setUserSet(true);
	EXPECT_TRUE(c.hasUser());
	c.setAuthenticated(true);
	EXPECT_TRUE(c.isAuthenticated());
	c.setRegistered(true);
	EXPECT_TRUE(c.isRegistered());
	c.setPingSent(true);
	EXPECT_TRUE(c.isPingSent());
}

TEST(ClientState, NicknameChange)
{
	Client c(103, "127.0.0.1");
	EXPECT_EQ(c.getNickname(), "*");
	c.setNickname("bob");
	EXPECT_EQ(c.getNickname(), "bob");
	c.setNickname("charlie");
	EXPECT_EQ(c.getNickname(), "charlie");
}

TEST(ClientState, UsernameRealname)
{
	Client c(104, "127.0.0.1");
	c.setUsername("myuser");
	c.setRealname("My Real Name");
	EXPECT_EQ(c.getUsername(), "myuser");
	EXPECT_EQ(c.getRealname(), "My Real Name");
}

TEST(ClientState, Password)
{
	Client c(105, "127.0.0.1");
	c.setPassword("secret123");
	EXPECT_EQ(c.getPassword(), "secret123");
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: ClientMemory — leak detection via PostMan
 * ════════════════════════════════════════════════════════════════════ */

extern int g_allocations;

TEST(ClientMemory, NoLeakOnCreateDestroy)
{
	int before;
	{
		Client *c = new Client(200, "127.0.0.1");
		c->setNickname("leaktest");
		c->queueMessage("test message");
		c->appendToRecvBuffer("NICK foo\r\n");
		std::vector<std::string> msgs = c->extractMessages();
		(void)msgs;
		before = g_allocations;
		delete c;
	}
	EXPECT_LE(g_allocations, before)
		<< "Client destruction should not leak memory";
}
