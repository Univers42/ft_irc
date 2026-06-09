/* ─── Security & robustness hardening tests ─── */

#include <gtest/gtest.h>
#include "PostMan.hpp"
#include "Client.hpp"
#include "Replies.hpp"

/* ════════════════════════════════════════════════════════════════════════
 * Suite: LineInjection — CR/LF/NUL can never smuggle extra IRC lines
 * ════════════════════════════════════════════════════════════════════ */

TEST(LineInjection, EmbeddedCRStrippedFromLine)
{
	Client c(60, "127.0.0.1");
	/* A stray \r mid-line must not survive into the parsed message — a
	 * relayed "text\rQUIT" would let peers' parsers see a forged line. */
	c.appendToRecvBuffer("PRIVMSG #chan :hello\rQUIT :bye\r\n");
	std::vector<std::string> msgs = c.extractMessages();
	ASSERT_EQ(msgs.size(), 1u);
	EXPECT_EQ(msgs[0], "PRIVMSG #chan :helloQUIT :bye");
	EXPECT_EQ(msgs[0].find('\r'), std::string::npos);
}

TEST(LineInjection, NulByteStripped)
{
	Client c(61, "127.0.0.1");
	std::string raw("PRIVMSG #chan :he");
	raw += '\0';
	raw += "llo\r\n";
	c.appendToRecvBuffer(raw);
	std::vector<std::string> msgs = c.extractMessages();
	ASSERT_EQ(msgs.size(), 1u);
	EXPECT_EQ(msgs[0], "PRIVMSG #chan :hello");
	EXPECT_EQ(msgs[0].find('\0'), std::string::npos);
}

TEST(LineInjection, CRLFSplitsAreSeparateMessagesNotInjection)
{
	Client c(62, "127.0.0.1");
	/* Full CRLF inside the stream is simply a line boundary — both halves
	 * are dispatched as their own commands through the normal gate. */
	c.appendToRecvBuffer("PRIVMSG #chan :a\r\nQUIT :bye\r\n");
	std::vector<std::string> msgs = c.extractMessages();
	ASSERT_EQ(msgs.size(), 2u);
	EXPECT_EQ(msgs[0], "PRIVMSG #chan :a");
	EXPECT_EQ(msgs[1], "QUIT :bye");
}

TEST(LineInjection, CtcpDccPayloadPassesUntouched)
{
	Client c(63, "127.0.0.1");
	/* \x01-framed CTCP (DCC SEND handshakes) must relay byte-for-byte. */
	std::string ctcp = "PRIVMSG bob :\x01" "DCC SEND file.bin 2130706433 5000 1234\x01";
	c.appendToRecvBuffer(ctcp + "\r\n");
	std::vector<std::string> msgs = c.extractMessages();
	ASSERT_EQ(msgs.size(), 1u);
	EXPECT_EQ(msgs[0], ctcp);
}

TEST(LineInjection, HighBitBytesPreserved)
{
	Client c(64, "127.0.0.1");
	std::string text = "PRIVMSG #chan :caf\xC3\xA9 \xFF\xFE";
	c.appendToRecvBuffer(text + "\r\n");
	std::vector<std::string> msgs = c.extractMessages();
	ASSERT_EQ(msgs.size(), 1u);
	EXPECT_EQ(msgs[0], text);
}
