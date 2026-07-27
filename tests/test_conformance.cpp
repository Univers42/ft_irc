/* ─── RFC 2812 conformance tests: line limits, invite lifetime, casemapping ───
 *
 * Every test here was written RED against the pre-fix tree (TESTING.md §1):
 *   - RfcLineLength.*      : outbound lines were never capped at 512, only the
 *                            INPUT LineBuffer knew MAX_MSGLEN. A relayed
 *                            PRIVMSG (prefix + text) or a big 353 exceeded it.
 *   - InviteLifetime.*     : invites were keyed by nickname string, so they
 *                            did not follow a NICK change and outlived the
 *                            invitee — a later client taking the freed nick
 *                            inherited the invite into a +i channel.
 *   - UserModeCase.*       : MODE <nick> compared the target with operator!=
 *                            instead of ircEquals, the one place in the tree
 *                            that bypassed CASEMAPPING=ascii.
 */

#include <gtest/gtest.h>
#include "PostMan.hpp"
#include "TestHarness.hpp"
#include "Client.hpp"
#include "Channel.hpp"
#include "Replies.hpp"

#include <vector>
#include <string>

class ConformanceTest : public IrcServerTest
{
protected:
	int portBase() const override { return 17700; }
};

/* Longest CRLF-terminated line in a received stream, counting the CRLF —
 * exactly the quantity RFC 2812 §2.3 caps at 512. */
static size_t maxLineLen(const std::string &data)
{
	size_t worst = 0;
	std::string::size_type start = 0;
	while (start < data.size())
	{
		std::string::size_type end = data.find("\r\n", start);
		if (end == std::string::npos)
			break;
		size_t len = (end - start) + 2;
		if (len > worst)
			worst = len;
		start = end + 2;
	}
	return worst;
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: RfcLineLength — no outbound line may exceed 512 bytes incl. CRLF
 * ════════════════════════════════════════════════════════════════════ */

TEST(RfcLineLength, QueuedLineIsCappedAtTheProtocolLimit)
{
	/* The choke point every reply/relay funnels through. */
	Client c(70, "127.0.0.1");
	c.queueMessage(std::string(600, 'x'));
	EXPECT_LE(c.getSendBuffer().size(), static_cast<size_t>(MAX_MSGLEN))
		<< "queueMessage must cap the line at 512 incl. CRLF";
	EXPECT_EQ(c.getSendBuffer().substr(c.getSendBuffer().size() - 2), "\r\n")
		<< "truncation must keep the line terminated";
}

TEST(RfcLineLength, ShortLinesAreUntouched)
{
	Client c(71, "127.0.0.1");
	c.queueMessage("PING :ft_irc");
	EXPECT_EQ(c.getSendBuffer(), "PING :ft_irc\r\n");
}

TEST(RfcLineLength, LineOfExactlyTheLimitSurvivesWhole)
{
	/* 510 payload + CRLF == 512: the largest legal line, must not be cut. */
	Client c(72, "127.0.0.1");
	c.queueMessage(std::string(MAX_MSGLEN - 2, 'y'));
	EXPECT_EQ(c.getSendBuffer().size(), static_cast<size_t>(MAX_MSGLEN));
	EXPECT_EQ(c.getSendBuffer().substr(0, 3), "yyy");
}

TEST_F(ConformanceTest, PrivmsgRelayStaysWithinLimit)
{
	/* The sender's own line is legal (<=512), but the relay re-frames it as
	 * ":nick!user@host PRIVMSG #chan :<text>" — strictly longer. */
	TestClient tx, rx;
	ASSERT_TRUE(tx.connect(serverPort));
	ASSERT_TRUE(rx.connect(serverPort));

	tx.registerClient("testpass", "sender9", "senderuser");
	rx.registerClient("testpass", "recver9", "recveruser");
	tx.recvAll(200);
	rx.recvAll(200);

	tx.sendCmd("JOIN #linelen");
	rx.sendCmd("JOIN #linelen");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	tx.recvAll(200);
	rx.recvAll(200);

	/* Fill the sender's line right up to the 512-byte input limit. */
	const std::string head = "PRIVMSG #linelen :";
	tx.sendCmd(head + std::string(MAX_MSGLEN - 2 - head.size(), 'z'));
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	std::string got = rx.recvAll(300);
	ASSERT_NE(got.find("PRIVMSG #linelen"), std::string::npos)
		<< "receiver never got the relayed message";
	EXPECT_LE(maxLineLen(got), static_cast<size_t>(MAX_MSGLEN))
		<< "relayed PRIVMSG exceeded the RFC 2812 line limit";

	tx.sendCmd("QUIT");
	rx.sendCmd("QUIT");
}

TEST_F(ConformanceTest, NamesReplyStaysWithinLimit)
{
	/* One 353 carrying every member overflows 512 well before the channel
	 * gets big: ~55 nine-char nicks is enough. The reply must be split
	 * across several 353 lines, and no nick may be lost in the process. */
	const int kMembers = 60;
	std::vector<TestClient *> crowd;
	std::vector<std::string> nicks;

	for (int i = 0; i < kMembers; ++i)
	{
		TestClient *tc = new TestClient();
		if (!tc->connect(serverPort))
		{
			delete tc;
			break;
		}
		char nick[16];
		std::snprintf(nick, sizeof(nick), "nmx%05d", i); /* 8 chars, unique */
		crowd.push_back(tc);
		nicks.push_back(nick);
		tc->sendCmd("PASS testpass");
		tc->sendCmd("NICK " + std::string(nick));
		tc->sendCmd("USER " + std::string(nick) + " 0 * :N");
		tc->sendCmd("JOIN #namesbig");
	}
	ASSERT_GE(crowd.size(), static_cast<size_t>(kMembers))
		<< "could not open enough connections to build a big channel";
	std::this_thread::sleep_for(std::chrono::milliseconds(600));

	/* A late joiner receives the full membership in its own 353 burst. */
	TestClient late;
	ASSERT_TRUE(late.connect(serverPort));
	late.registerClient("testpass", "latecomer", "lateuser");
	late.recvAll(200);
	late.sendCmd("JOIN #namesbig");
	std::this_thread::sleep_for(std::chrono::milliseconds(400));
	std::string got = late.recvAll(500);

	EXPECT_LE(maxLineLen(got), static_cast<size_t>(MAX_MSGLEN))
		<< "RPL_NAMREPLY exceeded the RFC 2812 line limit";

	/* Splitting must not silently drop members. */
	size_t missing = 0;
	for (size_t i = 0; i < nicks.size(); ++i)
	{
		if (got.find(nicks[i]) == std::string::npos)
			++missing;
	}
	EXPECT_EQ(missing, 0u) << missing << " members absent from the NAMES reply";

	for (size_t i = 0; i < crowd.size(); ++i)
		delete crowd[i];
}

TEST_F(ConformanceTest, LongUsernameCannotEatTheRelayPayload)
{
	/* Every relayed line is framed with the sender's prefix
	 * (nick!user@host). USER's username arrives straight off the wire, so an
	 * unbounded one inflates that prefix until the 512-byte cap starts
	 * eating the actual message -- silently, and for FILE DATA chunks it
	 * would corrupt the base64 rather than merely shorten chat text. The
	 * username must be bounded at registration. */
	TestClient tx, rx;
	ASSERT_TRUE(tx.connect(serverPort));
	ASSERT_TRUE(rx.connect(serverPort));

	tx.sendCmd("PASS testpass");
	tx.sendCmd("NICK ulen1");
	tx.sendCmd("USER " + std::string(480, 'u') + " 0 * :R");
	rx.registerClient("testpass", "ulen2", "ulen2");
	std::this_thread::sleep_for(std::chrono::milliseconds(250));
	tx.recvAll(200);
	rx.recvAll(200);

	tx.sendCmd("JOIN #ulen");
	rx.sendCmd("JOIN #ulen");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	tx.recvAll(200);
	rx.recvAll(200);

	tx.sendCmd("PRIVMSG #ulen :HELLO_MARKER_INTACT");
	std::this_thread::sleep_for(std::chrono::milliseconds(250));

	std::string got = rx.recvAll(300);
	EXPECT_NE(got.find("HELLO_MARKER_INTACT"), std::string::npos)
		<< "an oversized username pushed the payload past the line cap";
	EXPECT_LE(maxLineLen(got), static_cast<size_t>(MAX_MSGLEN));

	tx.sendCmd("QUIT");
	rx.sendCmd("QUIT");
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: InviteLifetime — an invite belongs to a connection, not to a name
 * ════════════════════════════════════════════════════════════════════ */

TEST_F(ConformanceTest, InviteDoesNotTransferToWhoeverTakesTheNick)
{
	/* host invites "invitee" to a +i channel; invitee renames itself and a
	 * stranger claims the freed nick. The stranger was never invited. */
	TestClient host, invitee;
	ASSERT_TRUE(host.connect(serverPort));
	ASSERT_TRUE(invitee.connect(serverPort));

	host.registerClient("testpass", "invhost", "invhost");
	invitee.registerClient("testpass", "invitee", "invitee");
	host.recvAll(200);
	invitee.recvAll(200);

	host.sendCmd("JOIN #inv1");
	host.sendCmd("MODE #inv1 +i");
	host.sendCmd("INVITE invitee #inv1");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	host.recvAll(200);
	invitee.recvAll(200);

	/* The invitee walks away from the name it was invited under. */
	invitee.sendCmd("NICK renamed1");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	invitee.recvAll(200);

	TestClient stranger;
	ASSERT_TRUE(stranger.connect(serverPort));
	stranger.registerClient("testpass", "invitee", "stranger");
	stranger.recvAll(200);
	stranger.sendCmd("JOIN #inv1");
	std::this_thread::sleep_for(std::chrono::milliseconds(250));

	std::string got = stranger.recvAll(300);
	EXPECT_NE(got.find(ERR_INVITEONLYCHAN), std::string::npos)
		<< "a stranger inherited an invite by taking the invitee's old nick";
	EXPECT_EQ(got.find("JOIN #inv1"), std::string::npos)
		<< "stranger joined a +i channel it was never invited to";

	host.sendCmd("QUIT");
	invitee.sendCmd("QUIT");
	stranger.sendCmd("QUIT");
}

TEST_F(ConformanceTest, InviteDoesNotOutliveTheInvitedConnection)
{
	/* Same hole, reached by disconnect rather than rename. */
	TestClient host, invitee;
	ASSERT_TRUE(host.connect(serverPort));
	ASSERT_TRUE(invitee.connect(serverPort));

	host.registerClient("testpass", "invhost2", "invhost2");
	invitee.registerClient("testpass", "ghost1", "ghost1");
	host.recvAll(200);
	invitee.recvAll(200);

	host.sendCmd("JOIN #inv2");
	host.sendCmd("MODE #inv2 +i");
	host.sendCmd("INVITE ghost1 #inv2");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	host.recvAll(200);

	invitee.sendCmd("QUIT :gone");
	invitee.disconnect();
	std::this_thread::sleep_for(std::chrono::milliseconds(250));

	TestClient successor;
	ASSERT_TRUE(successor.connect(serverPort));
	successor.registerClient("testpass", "ghost1", "successor");
	successor.recvAll(200);
	successor.sendCmd("JOIN #inv2");
	std::this_thread::sleep_for(std::chrono::milliseconds(250));

	std::string got = successor.recvAll(300);
	EXPECT_NE(got.find(ERR_INVITEONLYCHAN), std::string::npos)
		<< "an invite outlived its invitee and was inherited by a new client";

	host.sendCmd("QUIT");
	successor.sendCmd("QUIT");
}

TEST_F(ConformanceTest, InviteStillWorksForTheClientItWasIssuedTo)
{
	/* The guard above must not break the feature it guards. */
	TestClient host, invitee;
	ASSERT_TRUE(host.connect(serverPort));
	ASSERT_TRUE(invitee.connect(serverPort));

	host.registerClient("testpass", "invhost3", "invhost3");
	invitee.registerClient("testpass", "guest3", "guest3");
	host.recvAll(200);
	invitee.recvAll(200);

	host.sendCmd("JOIN #inv3");
	host.sendCmd("MODE #inv3 +i");
	host.sendCmd("INVITE guest3 #inv3");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	invitee.recvAll(200);

	invitee.sendCmd("JOIN #inv3");
	std::this_thread::sleep_for(std::chrono::milliseconds(250));
	std::string got = invitee.recvAll(300);
	EXPECT_NE(got.find("JOIN #inv3"), std::string::npos)
		<< "the invited client was refused its own invite";

	host.sendCmd("QUIT");
	invitee.sendCmd("QUIT");
}

TEST_F(ConformanceTest, InviteFollowsTheClientAcrossANickChange)
{
	/* The flip side: renaming must not cost you an invite you already hold. */
	TestClient host, invitee;
	ASSERT_TRUE(host.connect(serverPort));
	ASSERT_TRUE(invitee.connect(serverPort));

	host.registerClient("testpass", "invhost4", "invhost4");
	invitee.registerClient("testpass", "guest4", "guest4");
	host.recvAll(200);
	invitee.recvAll(200);

	host.sendCmd("JOIN #inv4");
	host.sendCmd("MODE #inv4 +i");
	host.sendCmd("INVITE guest4 #inv4");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	invitee.recvAll(200);

	invitee.sendCmd("NICK guest4b");
	std::this_thread::sleep_for(std::chrono::milliseconds(150));
	invitee.recvAll(200);
	invitee.sendCmd("JOIN #inv4");
	std::this_thread::sleep_for(std::chrono::milliseconds(250));

	std::string got = invitee.recvAll(300);
	EXPECT_NE(got.find("JOIN #inv4"), std::string::npos)
		<< "an invite was lost because its holder changed nick";

	host.sendCmd("QUIT");
	invitee.sendCmd("QUIT");
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: ChannelVisibility — a non-member learns nothing privileged
 * ════════════════════════════════════════════════════════════════════ */

TEST_F(ConformanceTest, ModeQueryDoesNotLeakTheChannelKeyToNonMembers)
{
	/* +k is the channel's password. Every state-*changing* path checks
	 * membership; the MODE query path did not, and RPL_CHANNELMODEIS
	 * carries the key as a parameter -- so any registered stranger could
	 * read it and walk straight in, defeating +k entirely. */
	TestClient owner, snoop;
	ASSERT_TRUE(owner.connect(serverPort));
	ASSERT_TRUE(snoop.connect(serverPort));

	owner.registerClient("testpass", "keyowner", "keyowner");
	snoop.registerClient("testpass", "keysnoop", "keysnoop");
	owner.recvAll(200);
	snoop.recvAll(200);

	owner.sendCmd("JOIN #keyed");
	owner.sendCmd("MODE #keyed +k s3cretkey");
	std::this_thread::sleep_for(std::chrono::milliseconds(250));
	owner.recvAll(200);

	snoop.sendCmd("MODE #keyed");
	std::this_thread::sleep_for(std::chrono::milliseconds(250));
	std::string got = snoop.recvAll(300);

	EXPECT_EQ(got.find("s3cretkey"), std::string::npos)
		<< "MODE query handed the +k key to a non-member";
	EXPECT_NE(got.find(ERR_NOTONCHANNEL), std::string::npos)
		<< "a non-member's MODE query should be refused with 442";

	owner.sendCmd("QUIT");
	snoop.sendCmd("QUIT");
}

TEST_F(ConformanceTest, MemberCanStillQueryChannelModes)
{
	/* The guard above must not break the legitimate query HexChat makes. */
	TestClient owner;
	ASSERT_TRUE(owner.connect(serverPort));
	owner.registerClient("testpass", "keyowner2", "keyowner2");
	owner.recvAll(200);

	owner.sendCmd("JOIN #keyed2");
	owner.sendCmd("MODE #keyed2 +k mykey");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	owner.recvAll(200);

	owner.sendCmd("MODE #keyed2");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	std::string got = owner.recvAll(300);
	EXPECT_NE(got.find(RPL_CHANNELMODEIS), std::string::npos)
		<< "a member must still be able to read its own channel's modes";
	EXPECT_NE(got.find("mykey"), std::string::npos)
		<< "a member is entitled to see the key";

	owner.sendCmd("QUIT");
}

TEST_F(ConformanceTest, UnregisteredConnectionIsNotAMessageTarget)
{
	/* A connection that sent only NICK never supplied the password. It must
	 * not be reachable: delivering to it would hand traffic to a peer that
	 * never passed the PASS gate the subject requires. */
	TestClient lurker, sender;
	ASSERT_TRUE(lurker.connect(serverPort));
	ASSERT_TRUE(sender.connect(serverPort));

	lurker.sendCmd("NICK ghosty");      /* no PASS, no USER */
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	lurker.recvAll(200);

	sender.registerClient("testpass", "realuser", "realuser");
	sender.recvAll(200);
	sender.sendCmd("PRIVMSG ghosty :secret");
	std::this_thread::sleep_for(std::chrono::milliseconds(250));

	EXPECT_NE(sender.recvAll(300).find(ERR_NOSUCHNICK), std::string::npos)
		<< "an unregistered connection was addressable as a nick";
	EXPECT_EQ(lurker.recvAll(300).find("secret"), std::string::npos)
		<< "traffic reached a connection that never sent PASS";

	sender.sendCmd("QUIT");
}

TEST_F(ConformanceTest, UnregisteredConnectionStillReservesItsNick)
{
	/* The flip side of the guard above: an unregistered connection must
	 * still hold the name it claimed, or two connections could both take it
	 * and both complete registration as the same nick. */
	TestClient holder, rival;
	ASSERT_TRUE(holder.connect(serverPort));
	ASSERT_TRUE(rival.connect(serverPort));

	holder.sendCmd("NICK heldnick");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	holder.recvAll(200);

	rival.sendCmd("PASS testpass");
	rival.sendCmd("NICK heldnick");
	std::this_thread::sleep_for(std::chrono::milliseconds(250));
	EXPECT_NE(rival.recvAll(300).find(ERR_NICKNAMEINUSE), std::string::npos)
		<< "a nick claimed by an unregistered connection was handed out twice";
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: ReplyWellFormedness — required numerics and canonical names
 * ════════════════════════════════════════════════════════════════════ */

TEST_F(ConformanceTest, EmptyPrivmsgTextIsRejected)
{
	/* "PRIVMSG #room :" parses to two params, the second empty -- it slipped
	 * past a bare size check and was relayed as an empty message. */
	TestClient tx, rx;
	ASSERT_TRUE(tx.connect(serverPort));
	ASSERT_TRUE(rx.connect(serverPort));
	tx.registerClient("testpass", "emptytx", "emptytx");
	rx.registerClient("testpass", "emptyrx", "emptyrx");
	tx.recvAll(200);
	rx.recvAll(200);

	tx.sendCmd("JOIN #empty");
	rx.sendCmd("JOIN #empty");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	tx.recvAll(200);
	rx.recvAll(200);

	tx.sendCmd("PRIVMSG #empty :");
	std::this_thread::sleep_for(std::chrono::milliseconds(250));

	EXPECT_NE(tx.recvAll(300).find(ERR_NOTEXTTOSEND), std::string::npos)
		<< "empty PRIVMSG text must draw 412";
	EXPECT_EQ(rx.recvAll(300).find("PRIVMSG #empty"), std::string::npos)
		<< "an empty message was relayed to the channel";

	tx.sendCmd("QUIT");
	rx.sendCmd("QUIT");
}

TEST_F(ConformanceTest, WhoisReportsChannelInItsDisplayCase)
{
	/* _channels is keyed by the casemapped name; the display name lives in
	 * Channel::_name. 319 printed the key, so a mixed-case channel came
	 * back lowercased and no longer matched what the client joined. */
	TestClient tc;
	ASSERT_TRUE(tc.connect(serverPort));
	tc.registerClient("testpass", "caseuser", "caseuser");
	tc.recvAll(200);

	tc.sendCmd("JOIN #MixedCase");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	tc.recvAll(200);
	tc.sendCmd("WHOIS caseuser");
	std::this_thread::sleep_for(std::chrono::milliseconds(250));

	std::string got = tc.recvAll(300);
	ASSERT_NE(got.find(RPL_WHOISCHANNELS), std::string::npos) << got;
	EXPECT_NE(got.find("#MixedCase"), std::string::npos)
		<< "WHOIS reported the casemapped key instead of the display name";

	tc.sendCmd("QUIT");
}

TEST_F(ConformanceTest, BareWhoStillSendsItsTerminator)
{
	/* A client that blocks until RPL_ENDOFWHO hangs forever if a bare WHO
	 * returns nothing at all. */
	TestClient tc;
	ASSERT_TRUE(tc.connect(serverPort));
	tc.registerClient("testpass", "whoterm", "whoterm");
	tc.recvAll(200);

	tc.sendCmd("WHO");
	std::this_thread::sleep_for(std::chrono::milliseconds(250));
	EXPECT_NE(tc.recvAll(300).find(RPL_ENDOFWHO), std::string::npos)
		<< "bare WHO produced no terminating 315";

	tc.sendCmd("QUIT");
}

TEST_F(ConformanceTest, BroadcastsUseCanonicalChannelAndNickCasing)
{
	/* Echoing the sender's spelling back to every member desyncs clients
	 * that match their channel/user lists by string. */
	TestClient owner, victim;
	ASSERT_TRUE(owner.connect(serverPort));
	ASSERT_TRUE(victim.connect(serverPort));
	owner.registerClient("testpass", "canonop", "canonop");
	victim.registerClient("testpass", "canonvic", "canonvic");
	owner.recvAll(200);
	victim.recvAll(200);

	owner.sendCmd("JOIN #CanonCase");
	std::this_thread::sleep_for(std::chrono::milliseconds(150));
	victim.sendCmd("JOIN #canoncase");   /* same channel, different spelling */
	std::this_thread::sleep_for(std::chrono::milliseconds(250));
	owner.recvAll(200);
	victim.recvAll(200);

	/* Address both the channel and the target nick in the "wrong" case. */
	owner.sendCmd("KICK #CANONCASE CANONVIC :bye");
	std::this_thread::sleep_for(std::chrono::milliseconds(250));

	std::string got = victim.recvAll(300);
	ASSERT_NE(got.find("KICK"), std::string::npos) << "no KICK received: " << got;
	EXPECT_NE(got.find("#CanonCase"), std::string::npos)
		<< "KICK echoed the sender's channel spelling, not the canonical one";
	EXPECT_NE(got.find("canonvic"), std::string::npos)
		<< "KICK echoed the sender's nick spelling, not the canonical one";

	owner.sendCmd("QUIT");
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: MalformedParams — an empty parameter is a missing parameter
 * ════════════════════════════════════════════════════════════════════ */

/* True if a line has two adjacent spaces *before* its trailing parameter --
 * i.e. an empty middle parameter was interpolated, which shifts every
 * following field for a parser that splits on spaces. Double spaces inside
 * the trailing parameter are legitimate message text and must not count. */
static bool hasEmptyMiddleParam(const std::string &line)
{
	std::string::size_type dbl = line.find("  ");
	if (dbl == std::string::npos)
		return false;
	std::string::size_type trailing = line.find(" :");
	return trailing == std::string::npos || dbl < trailing;
}

TEST(MalformedParamDetector, FlagsEmptyMiddleParamButNotMessageText)
{
	/* Guard the guard: this detector decides the suite below, so prove it
	 * discriminates rather than flagging (or ignoring) everything. */
	EXPECT_TRUE(hasEmptyMiddleParam(":ft_irc 315 bob  :End of WHO list"));
	EXPECT_TRUE(hasEmptyMiddleParam(":ft_irc 441 bob  #px :They aren't on that channel"));
	EXPECT_FALSE(hasEmptyMiddleParam(":ft_irc 332 bob #chan :hello  world"));
	EXPECT_FALSE(hasEmptyMiddleParam(":bob!u@h PRIVMSG #chan :a  b"));
	EXPECT_FALSE(hasEmptyMiddleParam(":bob!u@h JOIN #chan"));
}

TEST_F(ConformanceTest, EmptyTrailingParamNeverYieldsAMalformedReply)
{
	/* A bare ":" parses to a present-but-empty parameter. Interpolating that
	 * empty string into a numeric collapsed two spaces together, so a strict
	 * client reads the *next* field as the one that went missing. */
	TestClient tc, peer;
	ASSERT_TRUE(tc.connect(serverPort));
	ASSERT_TRUE(peer.connect(serverPort));
	tc.registerClient("testpass", "malfrm", "malfrm");
	peer.registerClient("testpass", "malpeer", "malpeer");
	tc.recvAll(200);
	peer.recvAll(200);

	tc.sendCmd("JOIN #malf");
	std::this_thread::sleep_for(std::chrono::milliseconds(150));
	tc.recvAll(200);

	static const char *kCases[] = {
		"WHO :", "WHOIS :", "TOPIC :", "PART :", "JOIN :",
		"KICK #malf :", "INVITE malpeer :", "PRIVMSG :", "MODE :",
		"USERHOST :", "NOTICE :"
	};
	for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); ++i)
	{
		tc.sendCmd(kCases[i]);
		std::this_thread::sleep_for(std::chrono::milliseconds(120));
		std::string got = tc.recvAll(200);

		std::string::size_type start = 0;
		while (start < got.size())
		{
			std::string::size_type end = got.find("\r\n", start);
			if (end == std::string::npos)
				break;
			std::string line = got.substr(start, end - start);
			EXPECT_FALSE(hasEmptyMiddleParam(line))
				<< "'" << kCases[i] << "' produced a malformed reply: " << line;
			start = end + 2;
		}
	}

	tc.sendCmd("QUIT");
	peer.sendCmd("QUIT");
}

TEST_F(ConformanceTest, EmptyRequiredParameterIsAnsweredNotIgnored)
{
	/* PART/JOIN with an empty target replied with nothing at all -- a client
	 * waiting on an answer just hangs. An empty name is a missing name. */
	TestClient tc;
	ASSERT_TRUE(tc.connect(serverPort));
	tc.registerClient("testpass", "needp", "needp");
	tc.recvAll(200);

	tc.sendCmd("JOIN :");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	EXPECT_NE(tc.recvAll(250).find(ERR_NEEDMOREPARAMS), std::string::npos)
		<< "JOIN with an empty channel name drew no reply";

	tc.sendCmd("PART :");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	EXPECT_NE(tc.recvAll(250).find(ERR_NEEDMOREPARAMS), std::string::npos)
		<< "PART with an empty channel name drew no reply";

	tc.sendCmd("QUIT");
}

TEST_F(ConformanceTest, RejectedChannelLimitIsReported)
{
	/* MODE +l with an unparseable/out-of-range limit was dropped on the
	 * floor: no mode change and no numeric, so the operator had no way to
	 * tell the request from a success. */
	TestClient tc;
	ASSERT_TRUE(tc.connect(serverPort));
	tc.registerClient("testpass", "limop", "limop");
	tc.recvAll(200);

	tc.sendCmd("JOIN #limt");
	std::this_thread::sleep_for(std::chrono::milliseconds(150));
	tc.recvAll(200);

	static const char *kBad[] = { "abc", "-5", "0", "99999999999999999999" };
	for (size_t i = 0; i < sizeof(kBad) / sizeof(kBad[0]); ++i)
	{
		tc.sendCmd(std::string("MODE #limt +l ") + kBad[i]);
		std::this_thread::sleep_for(std::chrono::milliseconds(150));
		std::string got = tc.recvAll(250);
		EXPECT_NE(got.find(ERR_INVALIDMODEPARAM), std::string::npos)
			<< "MODE +l " << kBad[i] << " was silently ignored";
		EXPECT_EQ(got.find("MODE #limt +l"), std::string::npos)
			<< "MODE +l " << kBad[i] << " must not take effect";
	}

	/* A good limit must still apply. */
	tc.sendCmd("MODE #limt +l 25");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	std::string ok = tc.recvAll(250);
	EXPECT_NE(ok.find("MODE #limt +l 25"), std::string::npos)
		<< "a valid limit was rejected";
	EXPECT_EQ(ok.find(ERR_INVALIDMODEPARAM), std::string::npos);

	tc.sendCmd("QUIT");
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: UserModeCase — MODE <nick> honours CASEMAPPING=ascii
 * ════════════════════════════════════════════════════════════════════ */

TEST_F(ConformanceTest, UserModeAcceptsOwnNickInAnyCase)
{
	TestClient tc;
	ASSERT_TRUE(tc.connect(serverPort));
	tc.registerClient("testpass", "CaseMode", "casemode");
	tc.recvAll(200);

	tc.sendCmd("MODE casemode +i");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	std::string got = tc.recvAll(300);
	EXPECT_EQ(got.find(ERR_USERSDONTMATCH), std::string::npos)
		<< "MODE on the client's own nick was rejected over letter case";

	tc.sendCmd("QUIT");
}

TEST_F(ConformanceTest, UserModeStillRejectsSomeoneElsesNick)
{
	TestClient tc;
	ASSERT_TRUE(tc.connect(serverPort));
	tc.registerClient("testpass", "modeself", "modeself");
	tc.recvAll(200);

	tc.sendCmd("MODE someoneelse +i");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	std::string got = tc.recvAll(300);
	EXPECT_NE(got.find(ERR_USERSDONTMATCH), std::string::npos)
		<< "MODE for another user's nick must still be refused";

	tc.sendCmd("QUIT");
}
