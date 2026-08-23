/* ─── Integration tests: full IRC protocol via TCP sockets ─── */

#include <poll.h>

#include "PostMan.hpp"
#include "TestHarness.hpp"
#include "ext/IServerExtension.hpp"

/* Fixture: server in a background thread (see TestHarness.hpp) */
class IntegrationTest : public IrcServerTest
{
protected:
	int portBase() const override { return 17100; }
};

/* ════════════════════════════════════════════════════════════════════════
 * Suite: ServerIntegration — Registration
 * ════════════════════════════════════════════════════════════════════ */

TEST_F(IntegrationTest, SuccessfulRegistration)
{
	TestClient tc;
	ASSERT_TRUE(tc.connect(serverPort));

	tc.sendCmd("PASS testpass");
	tc.sendCmd("NICK integ1");
	tc.sendCmd("USER integ1 0 * :Integration Test");
	std::this_thread::sleep_for(std::chrono::milliseconds(300));

	std::string reply = tc.recvAll();
	EXPECT_TRUE(tc.hasNumeric(reply, "001")) << "Expected RPL_WELCOME";
	EXPECT_TRUE(tc.hasNumeric(reply, "002")) << "Expected RPL_YOURHOST";
	EXPECT_TRUE(tc.hasNumeric(reply, "003")) << "Expected RPL_CREATED";
	EXPECT_TRUE(tc.hasNumeric(reply, "004")) << "Expected RPL_MYINFO";
	EXPECT_TRUE(tc.hasNumeric(reply, "005")) << "Expected RPL_ISUPPORT";
	EXPECT_TRUE(tc.hasNumeric(reply, "422")) << "Expected ERR_NOMOTD";

	tc.sendCmd("QUIT");
}

/* T4: disconnectClient() now defers the physical close until the queued
   reply drains via the existing EPOLLOUT path, so ERR_PASSWDMISMATCH
   (464), queued by sendReply() right before disconnectClient() is called
   in completeRegistration(), actually reaches the client before the
   socket closes. This is the fire test for that mechanism: it fails
   against pre-T4 code (0 bytes delivered, silent close) and passes once
   the deferred-close path is in place. */
TEST_F(IntegrationTest, PasswordMismatchReplyDeliveredBeforeClose)
{
	TestClient tc;
	ASSERT_TRUE(tc.connect(serverPort));

	tc.sendCmd("PASS wrongpass");
	tc.sendCmd("NICK fireTest");
	tc.sendCmd("USER fireTest 0 * :Test");

	std::string reply = tc.recvAll();
	EXPECT_TRUE(tc.hasNumeric(reply, "464"))
		<< "Expected ERR_PASSWDMISMATCH to reach the client before close";

	char buf[64];
	ssize_t n = recv(tc.fd(), buf, sizeof(buf), 0);
	EXPECT_EQ(n, 0) << "Expected the socket to close after 464 was delivered";
}

TEST_F(IntegrationTest, WrongPassword)
{
	TestClient tc;
	ASSERT_TRUE(tc.connect(serverPort));

	tc.sendCmd("PASS wrongpass");
	tc.sendCmd("NICK wrongpw");
	tc.sendCmd("USER wrongpw 0 * :Test");

	/* T4: the deferred-close mechanism flushes the queued 464 before
	   closing the fd (see PasswordMismatchReplyDeliveredBeforeClose). */
	std::string reply = tc.recvAll();
	EXPECT_TRUE(tc.hasNumeric(reply, "464"))
		<< "Expected ERR_PASSWDMISMATCH to reach the client before close";
}

TEST_F(IntegrationTest, NoPassword)
{
	TestClient tc;
	ASSERT_TRUE(tc.connect(serverPort));

	tc.sendCmd("NICK nopw");
	tc.sendCmd("USER nopw 0 * :Test");

	/* Same path as WrongPassword (!hasPassSent() hits the same branch in
	   completeRegistration) -- same T4 contract. */
	std::string reply = tc.recvAll();
	EXPECT_TRUE(tc.hasNumeric(reply, "464"))
		<< "Expected ERR_PASSWDMISMATCH to reach the client before close";
}

TEST_F(IntegrationTest, UnregisteredCommand)
{
	TestClient tc;
	ASSERT_TRUE(tc.connect(serverPort));

	tc.sendCmd("JOIN #test");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	std::string reply = tc.recvAll();
	EXPECT_TRUE(tc.hasNumeric(reply, "451")) << "Expected ERR_NOTREGISTERED";
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: ServerIntegration — Channels
 * ════════════════════════════════════════════════════════════════════ */

TEST_F(IntegrationTest, JoinChannel)
{
	TestClient tc;
	ASSERT_TRUE(tc.connect(serverPort));
	tc.registerClient("testpass", "joiner", "joiner");
	tc.recvAll(); /* consume welcome */

	tc.sendCmd("JOIN #inttest");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	std::string reply = tc.recvAll();
	EXPECT_NE(reply.find("JOIN"), std::string::npos) << "Expected JOIN echo";
	EXPECT_TRUE(tc.hasNumeric(reply, "353")) << "Expected RPL_NAMREPLY";
	EXPECT_TRUE(tc.hasNumeric(reply, "366")) << "Expected RPL_ENDOFNAMES";
	EXPECT_TRUE(tc.hasNumeric(reply, "324")) << "Expected RPL_CHANNELMODEIS on join";
	EXPECT_TRUE(tc.hasNumeric(reply, "329")) << "Expected RPL_CREATIONTIME on join";

	tc.sendCmd("QUIT");
}

TEST_F(IntegrationTest, ChannelMessage)
{
	TestClient tc1, tc2;
	ASSERT_TRUE(tc1.connect(serverPort));
	ASSERT_TRUE(tc2.connect(serverPort));

	tc1.registerClient("testpass", "sender1", "sender1");
	tc2.registerClient("testpass", "recver1", "recver1");
	tc1.recvAll();
	tc2.recvAll();

	tc1.sendCmd("JOIN #msgtest");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	tc1.recvAll();

	tc2.sendCmd("JOIN #msgtest");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	tc1.recvAll(); /* tc1 gets tc2's JOIN */
	tc2.recvAll(); /* tc2 gets own JOIN + names */

	tc1.sendCmd("PRIVMSG #msgtest :hello from sender1");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	std::string r2 = tc2.recvAll();
	EXPECT_NE(r2.find("hello from sender1"), std::string::npos)
		<< "Receiver should get channel message";

	tc1.sendCmd("QUIT");
	tc2.sendCmd("QUIT");
}

TEST_F(IntegrationTest, PrivateMessage)
{
	TestClient tc1, tc2;
	ASSERT_TRUE(tc1.connect(serverPort));
	ASSERT_TRUE(tc2.connect(serverPort));

	tc1.registerClient("testpass", "pmsend", "pmsend");
	tc2.registerClient("testpass", "pmrecv", "pmrecv");
	tc1.recvAll();
	tc2.recvAll();

	tc1.sendCmd("PRIVMSG pmrecv :private hello");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	std::string r2 = tc2.recvAll();
	EXPECT_NE(r2.find("private hello"), std::string::npos)
		<< "Should receive private message";

	tc1.sendCmd("QUIT");
	tc2.sendCmd("QUIT");
}

TEST_F(IntegrationTest, PrivmsgNoRecipientGivesErrNorecipient)
{
	TestClient tc;
	ASSERT_TRUE(tc.connect(serverPort));
	tc.registerClient("testpass", "norecip", "norecip");
	tc.recvAll();

	/* No target, no text at all (CommandMessaging.cpp:14-19) */
	tc.sendCmd("PRIVMSG");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	std::string r = tc.recvAll();
	if (!tc.hasNumeric(r, "411"))
		r += tc.recvAll(200);
	EXPECT_TRUE(tc.hasNumeric(r, "411"))
		<< "PRIVMSG with no target and no text should be denied with ERR_NORECIPIENT";

	tc.sendCmd("QUIT");
}

TEST_F(IntegrationTest, PrivmsgNoTextGivesErrNotextToSend)
{
	TestClient tc;
	ASSERT_TRUE(tc.connect(serverPort));
	tc.registerClient("testpass", "notext", "notext");
	tc.recvAll();

	/* Target deliberately unresolvable: the text check (params.size() < 2,
	 * CommandMessaging.cpp:20-24) runs before findClientByNick, so a
	 * nonexistent target still yields 412, not 401 — confirmed below. */
	tc.sendCmd("PRIVMSG nosuchnick_412");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	std::string r = tc.recvAll();
	if (!tc.hasNumeric(r, "412"))
		r += tc.recvAll(200);
	EXPECT_TRUE(tc.hasNumeric(r, "412"))
		<< "PRIVMSG with a target but no text should be denied with ERR_NOTEXTTOSEND";
	EXPECT_FALSE(tc.hasNumeric(r, "401"));

	tc.sendCmd("QUIT");
}

TEST_F(IntegrationTest, PrivmsgNoSuchNickGivesErrNosuchnick)
{
	TestClient tc;
	ASSERT_TRUE(tc.connect(serverPort));
	tc.registerClient("testpass", "ghostsend", "ghostsend");
	tc.recvAll();

	tc.sendCmd("PRIVMSG ghost_nick_xyz :hola");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	std::string r = tc.recvAll();
	if (!tc.hasNumeric(r, "401"))
		r += tc.recvAll(200);
	EXPECT_TRUE(tc.hasNumeric(r, "401"))
		<< "PRIVMSG to a nonexistent nick should be denied with ERR_NOSUCHNICK";

	tc.sendCmd("QUIT");
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: ServerIntegration — Operator commands
 * ════════════════════════════════════════════════════════════════════ */

TEST_F(IntegrationTest, TopicSetAndQuery)
{
	TestClient tc;
	ASSERT_TRUE(tc.connect(serverPort));
	tc.registerClient("testpass", "topicop", "topicop");
	tc.recvAll();

	tc.sendCmd("JOIN #topictest");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	tc.recvAll();

	tc.sendCmd("TOPIC #topictest :My Cool Topic");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	std::string r1 = tc.recvAll();
	EXPECT_NE(r1.find("My Cool Topic"), std::string::npos);

	tc.sendCmd("TOPIC #topictest");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	std::string r2 = tc.recvAll();
	EXPECT_TRUE(tc.hasNumeric(r2, "332")) << "Expected RPL_TOPIC";

	tc.sendCmd("QUIT");
}

TEST_F(IntegrationTest, KickUser)
{
	TestClient op, victim;
	ASSERT_TRUE(op.connect(serverPort));
	ASSERT_TRUE(victim.connect(serverPort));

	op.registerClient("testpass", "kickop", "kickop");
	victim.registerClient("testpass", "kicked", "kicked");
	op.recvAll();
	victim.recvAll();

	op.sendCmd("JOIN #kicktest");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	op.recvAll();

	victim.sendCmd("JOIN #kicktest");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	op.recvAll();
	victim.recvAll();

	op.sendCmd("KICK #kicktest kicked :bye");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	std::string vr = victim.recvAll();
	EXPECT_NE(vr.find("KICK"), std::string::npos) << "Victim should see KICK";

	op.sendCmd("QUIT");
	victim.sendCmd("QUIT");
}

TEST_F(IntegrationTest, InviteToChannel)
{
	TestClient op, invitee;
	ASSERT_TRUE(op.connect(serverPort));
	ASSERT_TRUE(invitee.connect(serverPort));

	op.registerClient("testpass", "invop", "invop");
	invitee.registerClient("testpass", "invited", "invited");
	op.recvAll();
	invitee.recvAll();

	op.sendCmd("JOIN #invtest");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	op.recvAll();

	/* Set invite-only */
	op.sendCmd("MODE #invtest +i");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	op.recvAll();

	/* Invite the user */
	op.sendCmd("INVITE invited #invtest");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	std::string opr = op.recvAll();
	EXPECT_TRUE(op.hasNumeric(opr, "341")) << "Expected RPL_INVITING";

	std::string invr = invitee.recvAll();
	EXPECT_NE(invr.find("INVITE"), std::string::npos)
		<< "Invitee should receive INVITE";

	/* Now invitee should be able to join */
	invitee.sendCmd("JOIN #invtest");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	std::string jr = invitee.recvAll();
	EXPECT_NE(jr.find("JOIN"), std::string::npos) << "Invitee should join";

	op.sendCmd("QUIT");
	invitee.sendCmd("QUIT");
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: ServerIntegration — Operator commands (permission checks)
 * ════════════════════════════════════════════════════════════════════ */

TEST_F(IntegrationTest, KickDeniedForNonOperator)
{
	TestClient op, member;
	ASSERT_TRUE(op.connect(serverPort));
	ASSERT_TRUE(member.connect(serverPort));

	op.registerClient("testpass", "kdop", "kdop");
	member.registerClient("testpass", "kdmem", "kdmem");
	op.recvAll();
	member.recvAll();

	op.sendCmd("JOIN #kickdenied");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	op.recvAll();

	member.sendCmd("JOIN #kickdenied");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	op.recvAll();
	member.recvAll();

	/* Non-operator attempts to KICK the operator */
	member.sendCmd("KICK #kickdenied kdop :bye");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	std::string mr = member.recvAll();
	EXPECT_TRUE(member.hasNumeric(mr, "482"))
		<< "Non-operator KICK should be denied with ERR_CHANOPRIVSNEEDED";

	op.sendCmd("QUIT");
	member.sendCmd("QUIT");
}

TEST_F(IntegrationTest, ModeDeniedForNonOperator)
{
	TestClient op, member;
	ASSERT_TRUE(op.connect(serverPort));
	ASSERT_TRUE(member.connect(serverPort));

	op.registerClient("testpass", "mdop", "mdop");
	member.registerClient("testpass", "mdmem", "mdmem");
	op.recvAll();
	member.recvAll();

	op.sendCmd("JOIN #modedenied");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	op.recvAll();

	member.sendCmd("JOIN #modedenied");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	op.recvAll();
	member.recvAll();

	/* +t is the representative flag: handleChannelMode gates i,t,k,o,l
	 * behind a single isOperator() check (CommandOperator.cpp:294) — the
	 * gate isn't per-flag, so testing the other flags would be redundant. */
	member.sendCmd("MODE #modedenied +t");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	std::string mr = member.recvAll();
	EXPECT_TRUE(member.hasNumeric(mr, "482"))
		<< "Non-operator MODE should be denied with ERR_CHANOPRIVSNEEDED";

	op.sendCmd("QUIT");
	member.sendCmd("QUIT");
}

TEST_F(IntegrationTest, TopicDeniedForNonOperatorWhenRestricted)
{
	TestClient op, member;
	ASSERT_TRUE(op.connect(serverPort));
	ASSERT_TRUE(member.connect(serverPort));

	op.registerClient("testpass", "tdop", "tdop");
	member.registerClient("testpass", "tdmem", "tdmem");
	op.recvAll();
	member.recvAll();

	op.sendCmd("JOIN #topicdenied");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	op.recvAll();

	member.sendCmd("JOIN #topicdenied");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	op.recvAll();
	member.recvAll();

	op.sendCmd("MODE #topicdenied +t");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	op.recvAll();
	member.recvAll();

	member.sendCmd("TOPIC #topicdenied :hijacked");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	std::string mr = member.recvAll();
	EXPECT_TRUE(member.hasNumeric(mr, "482"))
		<< "Non-operator TOPIC should be denied with ERR_CHANOPRIVSNEEDED when +t is set";

	op.sendCmd("QUIT");
	member.sendCmd("QUIT");
}

TEST_F(IntegrationTest, TopicAllowedForNonOperatorWhenNotRestricted)
{
	TestClient op, member;
	ASSERT_TRUE(op.connect(serverPort));
	ASSERT_TRUE(member.connect(serverPort));

	op.registerClient("testpass", "taop", "taop");
	member.registerClient("testpass", "tamem", "tamem");
	op.recvAll();
	member.recvAll();

	op.sendCmd("JOIN #topicallowed");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	op.recvAll();

	member.sendCmd("JOIN #topicallowed");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	op.recvAll();
	member.recvAll();

	/* No +t set: TOPIC is unrestricted, any member may set it */
	member.sendCmd("TOPIC #topicallowed :allowed topic");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	std::string mr = member.recvAll();
	/* Command already sent — if the signal hasn't arrived yet, drain a
	 * second short window rather than resend, to close the race without
	 * assuming a fixed round-trip latency. */
	if (mr.find("allowed topic") == std::string::npos)
		mr += member.recvAll(200);
	EXPECT_NE(mr.find("allowed topic"), std::string::npos)
		<< "Non-operator TOPIC should be applied and propagated when +t is not set";
	EXPECT_FALSE(member.hasNumeric(mr, "482"));

	op.sendCmd("QUIT");
	member.sendCmd("QUIT");
}

TEST_F(IntegrationTest, InviteDeniedForNonOperatorWhenInviteOnly)
{
	TestClient op, member;
	ASSERT_TRUE(op.connect(serverPort));
	ASSERT_TRUE(member.connect(serverPort));

	op.registerClient("testpass", "idop", "idop");
	member.registerClient("testpass", "idmem", "idmem");
	op.recvAll();
	member.recvAll();

	op.sendCmd("JOIN #invdenied");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	op.recvAll();

	member.sendCmd("JOIN #invdenied");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	op.recvAll();
	member.recvAll();

	op.sendCmd("MODE #invdenied +i");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	op.recvAll();
	member.recvAll();

	/* Non-operator attempts to INVITE; target need not exist — the 482
	 * gate fires before findClientByNick (CommandOperator.cpp:110-117) */
	member.sendCmd("INVITE idghost #invdenied");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	std::string mr = member.recvAll();
	EXPECT_TRUE(member.hasNumeric(mr, "482"))
		<< "Non-operator INVITE should be denied with ERR_CHANOPRIVSNEEDED when +i is set";

	op.sendCmd("QUIT");
	member.sendCmd("QUIT");
}

TEST_F(IntegrationTest, InviteAllowedForNonOperatorWhenNotInviteOnly)
{
	TestClient op, member, guest;
	ASSERT_TRUE(op.connect(serverPort));
	ASSERT_TRUE(member.connect(serverPort));
	ASSERT_TRUE(guest.connect(serverPort));

	op.registerClient("testpass", "iaop", "iaop");
	member.registerClient("testpass", "iamem", "iamem");
	guest.registerClient("testpass", "iaguest", "iaguest");
	op.recvAll();
	member.recvAll();
	guest.recvAll();

	op.sendCmd("JOIN #invallowed");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	op.recvAll();

	member.sendCmd("JOIN #invallowed");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	op.recvAll();
	member.recvAll();

	/* No +i set: INVITE is unrestricted, any member may invite */
	member.sendCmd("INVITE iaguest #invallowed");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	std::string mr = member.recvAll();
	/* Command already sent — drain once more instead of resending if the
	 * numeric hasn't landed yet, closing the race without a fixed sleep. */
	if (!member.hasNumeric(mr, "341"))
		mr += member.recvAll(200);
	EXPECT_TRUE(member.hasNumeric(mr, "341"))
		<< "Non-operator INVITE should succeed (RPL_INVITING) when +i is not set";
	EXPECT_FALSE(member.hasNumeric(mr, "482"));

	op.sendCmd("QUIT");
	member.sendCmd("QUIT");
	guest.sendCmd("QUIT");
}

/* 404 here proves non-membership specifically: in this codebase
 * ERR_CANNOTSENDTOCHAN has a single cause (!isMember). If +n/+m are
 * ever added, this check needs a mode-independent membership probe. */
TEST_F(IntegrationTest, JoinInviteOnlyDeniedWithoutInvite)
{
	TestClient op, member;
	ASSERT_TRUE(op.connect(serverPort));
	ASSERT_TRUE(member.connect(serverPort));

	op.registerClient("testpass", "ijop", "ijop");
	member.registerClient("testpass", "ijmem", "ijmem");
	op.recvAll();
	member.recvAll();

	op.sendCmd("JOIN #ijoin");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	op.recvAll();

	op.sendCmd("MODE #ijoin +i");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	op.recvAll();

	/* member was never invited */
	member.sendCmd("JOIN #ijoin");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	std::string mr = member.recvAll();
	if (!member.hasNumeric(mr, "473"))
		mr += member.recvAll(200);
	EXPECT_TRUE(member.hasNumeric(mr, "473"))
		<< "JOIN to an invite-only channel without an invite should be denied with ERR_INVITEONLYCHAN";

	/* Positive proof of non-membership: a non-member can't send to the
	 * channel either (404), confirming the JOIN was actually rejected. */
	member.sendCmd("PRIVMSG #ijoin :test");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	std::string pr = member.recvAll();
	if (!member.hasNumeric(pr, "404"))
		pr += member.recvAll(200);
	EXPECT_TRUE(member.hasNumeric(pr, "404"))
		<< "Non-member must not be able to PRIVMSG the channel it was denied JOIN to";

	op.sendCmd("QUIT");
	member.sendCmd("QUIT");
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: ServerIntegration — Modes
 * ════════════════════════════════════════════════════════════════════ */

TEST_F(IntegrationTest, ChannelModeQuery)
{
	TestClient tc;
	ASSERT_TRUE(tc.connect(serverPort));
	tc.registerClient("testpass", "modeq", "modeq");
	tc.recvAll();

	tc.sendCmd("JOIN #modeqtest");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	tc.recvAll();

	tc.sendCmd("MODE #modeqtest");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	std::string reply = tc.recvAll();
	EXPECT_TRUE(tc.hasNumeric(reply, "324")) << "Expected RPL_CHANNELMODEIS";
	EXPECT_TRUE(tc.hasNumeric(reply, "329")) << "Expected RPL_CREATIONTIME";

	tc.sendCmd("QUIT");
}

TEST_F(IntegrationTest, ChannelModeKey)
{
	TestClient op, joiner;
	ASSERT_TRUE(op.connect(serverPort));
	ASSERT_TRUE(joiner.connect(serverPort));

	op.registerClient("testpass", "keyop", "keyop");
	joiner.registerClient("testpass", "keyjoin", "keyjoin");
	op.recvAll();
	joiner.recvAll();

	op.sendCmd("JOIN #keytest");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	op.recvAll();

	op.sendCmd("MODE #keytest +k mysecret");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	op.recvAll();

	/* Try to join without key — should fail */
	joiner.sendCmd("JOIN #keytest");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	std::string r1 = joiner.recvAll();
	EXPECT_TRUE(joiner.hasNumeric(r1, "475")) << "Expected ERR_BADCHANNELKEY";

	/* Join with correct key — should succeed */
	joiner.sendCmd("JOIN #keytest mysecret");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	std::string r2 = joiner.recvAll();
	EXPECT_NE(r2.find("JOIN"), std::string::npos) << "Should join with key";

	op.sendCmd("QUIT");
	joiner.sendCmd("QUIT");
}

TEST_F(IntegrationTest, ChannelModeLimit)
{
	TestClient op, joiner;
	ASSERT_TRUE(op.connect(serverPort));
	ASSERT_TRUE(joiner.connect(serverPort));

	op.registerClient("testpass", "limop", "limop");
	joiner.registerClient("testpass", "limjoin", "limjoin");
	op.recvAll();
	joiner.recvAll();

	op.sendCmd("JOIN #limtest");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	op.recvAll();

	op.sendCmd("MODE #limtest +l 1");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	op.recvAll();

	/* Channel full — should fail */
	joiner.sendCmd("JOIN #limtest");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	std::string r = joiner.recvAll();
	EXPECT_TRUE(joiner.hasNumeric(r, "471")) << "Expected ERR_CHANNELISFULL";

	op.sendCmd("QUIT");
	joiner.sendCmd("QUIT");
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: ServerIntegration — Partial data reassembly
 * ════════════════════════════════════════════════════════════════════ */

TEST_F(IntegrationTest, PartialDataReassembly)
{
	TestClient tc;
	ASSERT_TRUE(tc.connect(serverPort));

	/* Send registration in fragments */
	tc.sendRaw("PASS test");
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	tc.sendRaw("pass\r\n");
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	tc.sendRaw("NI");
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	tc.sendRaw("CK partial1\r\n");
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	tc.sendRaw("USER par");
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	tc.sendRaw("tial1 0 * :Test\r\n");
	std::this_thread::sleep_for(std::chrono::milliseconds(300));

	std::string reply = tc.recvAll();
	EXPECT_TRUE(tc.hasNumeric(reply, "001"))
		<< "Should register after reassembled partial messages";

	tc.sendCmd("QUIT");
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: ServerIntegration — Nick change
 * ════════════════════════════════════════════════════════════════════ */

TEST_F(IntegrationTest, NickChange)
{
	TestClient tc1, tc2;
	ASSERT_TRUE(tc1.connect(serverPort));
	ASSERT_TRUE(tc2.connect(serverPort));

	tc1.registerClient("testpass", "oldnick", "oldnick");
	tc2.registerClient("testpass", "observer", "observer");
	tc1.recvAll();
	tc2.recvAll();

	tc1.sendCmd("JOIN #nickchange");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	tc1.recvAll();

	tc2.sendCmd("JOIN #nickchange");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	tc1.recvAll();
	tc2.recvAll();

	tc1.sendCmd("NICK newnick");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	std::string r2 = tc2.recvAll();
	EXPECT_NE(r2.find("NICK"), std::string::npos) << "Observer should see NICK change";
	EXPECT_NE(r2.find("newnick"), std::string::npos);

	tc1.sendCmd("QUIT");
	tc2.sendCmd("QUIT");
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: ServerIntegration — Query commands
 * ════════════════════════════════════════════════════════════════════ */

TEST_F(IntegrationTest, WhoChannel)
{
	TestClient tc;
	ASSERT_TRUE(tc.connect(serverPort));
	tc.registerClient("testpass", "whotest", "whotest");
	tc.recvAll();

	tc.sendCmd("JOIN #whotest");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	tc.recvAll();

	tc.sendCmd("WHO #whotest");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	std::string reply = tc.recvAll();
	EXPECT_TRUE(tc.hasNumeric(reply, "352")) << "Expected RPL_WHOREPLY";
	EXPECT_TRUE(tc.hasNumeric(reply, "315")) << "Expected RPL_ENDOFWHO";

	tc.sendCmd("QUIT");
}

TEST_F(IntegrationTest, WhoisUser)
{
	TestClient tc;
	ASSERT_TRUE(tc.connect(serverPort));
	tc.registerClient("testpass", "whoistest", "whoistest");
	tc.recvAll();

	tc.sendCmd("WHOIS whoistest");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	std::string reply = tc.recvAll();
	EXPECT_TRUE(tc.hasNumeric(reply, "311")) << "Expected RPL_WHOISUSER";
	EXPECT_TRUE(tc.hasNumeric(reply, "318")) << "Expected RPL_ENDOFWHOIS";

	tc.sendCmd("QUIT");
}

TEST_F(IntegrationTest, UnknownCommand)
{
	TestClient tc;
	ASSERT_TRUE(tc.connect(serverPort));
	tc.registerClient("testpass", "unkncmd", "unkncmd");
	tc.recvAll();

	tc.sendCmd("FOOBAR");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	std::string reply = tc.recvAll();
	EXPECT_TRUE(tc.hasNumeric(reply, "421")) << "Expected ERR_UNKNOWNCOMMAND";

	tc.sendCmd("QUIT");
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: ServerIntegration — Bot via PRIVMSG
 * ════════════════════════════════════════════════════════════════════ */

TEST_F(IntegrationTest, BotViaPrivmsg)
{
	TestClient tc;
	ASSERT_TRUE(tc.connect(serverPort));
	tc.registerClient("testpass", "botuser", "botuser");
	tc.recvAll();

	tc.sendCmd("PRIVMSG ircbot :!help");
	std::this_thread::sleep_for(std::chrono::milliseconds(300));

	std::string reply = tc.recvAll();
	EXPECT_NE(reply.find("Available commands"), std::string::npos)
		<< "Should receive bot help response";

	tc.sendCmd("QUIT");
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: ServerIntegration — Deferred disconnect deadline (T4)
 *
 * A payload that fits in one send() drains almost instantly on loopback
 * regardless of a shrunk receive window -- Linux's receive-buffer
 * autotuning (net.ipv4.tcp_moderate_rcvbuf) reopens the window within a
 * couple hundred milliseconds even against a peer that never calls recv(),
 * so simulating a genuinely-stuck TCP peer via socket-buffer tuning is not
 * reliable across machines (the same class of environment sensitivity the
 * T6 frozen-reader tests already documented for buffer-size thresholds).
 * Instead, this probe keeps _out topped up (just under MAX_SENDQ, refilled
 * every tick) for as long as the client exists, so drain-complete can
 * never be what closes the connection -- only checkPendingCloseTimeouts()
 * can, once PENDING_CLOSE_TIMEOUT elapses. This tests the deadline itself
 * as an unconditional ceiling, deterministically.
 * ════════════════════════════════════════════════════════════════════ */

/* Fires once a client registers: queues an initial payload just under
   MAX_SENDQ and defers a disconnect on it, then keeps topping _out back up
   every tick for as long as the client still exists, so it never drains to
   empty on its own. Used only by DeferredCloseDeadlineTest. */
struct DeadlineRefillProbe : public IServerExtension
{
	int targetFd;
	bool claimed; /* a subject was chosen once; never adopt another */

	DeadlineRefillProbe() : targetFd(-1), claimed(false) {}

	const char *name() const override { return "deadline-refill-probe"; }

	/* Build `bytes` of backlog out of maximum-length *legal* lines.
	** Queueing it as one huge line does not work: Client::queueMessage caps
	** every line at the RFC 2812 limit of 512 bytes incl. CRLF, so a single
	** 50 KB "line" would arrive as 512 bytes and drain instantly -- which
	** would make this test pass or fail on drain timing rather than on the
	** deadline it exists to pin down. 502 payload bytes + sendToClient's
	** ":ft_irc " prefix (8) = 510, +CRLF = exactly the 512-byte limit, so
	** nothing is truncated and each call adds a predictable 512 bytes. */
	static void queueBacklog(Server &server, Client *client, size_t bytes)
	{
		for (size_t queued = 0; queued < bytes; queued += 512)
			server.sendToClient(client, std::string(502, 'A'));
	}

	void onClientRegistered(Server &server, Client &client) override
	{
		/* Only the FIRST client that registers is the subject.
		**
		** The guard is `claimed`, not `targetFd != -1`: onTick() resets
		** targetFd to -1 once the subject is finalized, so an fd-based test
		** goes back to accepting candidates. This test then opens a second,
		** healthy connection (hb) to keep the event loop ticking -- and the
		** probe adopted it, queued 50 KB at it and disconnected it too, so
		** the heartbeat meant to drive checkPendingCloseTimeouts() was
		** itself pending-close. */
		if (claimed)
			return;
		claimed = true;

		targetFd = client.getFd();

		/* Clamp the SERVER's socket send buffer.
		**
		** Without this the test cannot work at all, and this is what made
		** it fail: Client::_out is USERSPACE. send() moves bytes into the
		** kernel's socket buffer, and _out empties as soon as the kernel
		** accepts them -- whether or not the peer ever reads. The default
		** tcp_wmem here starts at 16 KB and autotunes towards
		** wmem_max (208 KB on this box), so a backlog of 50 KB vanishes
		** into the kernel, hasPendingData() goes false, and
		** handleClientOutput() finalizes on the DRAIN-COMPLETE path in a
		** couple of hundred milliseconds -- long before any deadline.
		**
		** Raising the backlog instead is not an option: it must stay under
		** MAX_SENDQ (64 KiB) or this turns into a SendQ close, and 64 KiB
		** is far below what the kernel will absorb. Shrinking the pipe is
		** the only lever that leaves _out non-empty.
		**
		** The client shrinks its own receive buffer symmetrically (see the
		** SO_RCVBUF call in the test body); both ends are needed, since
		** either one alone still leaves room for the whole backlog. */
		int sndbuf = 4096;
		setsockopt(targetFd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

		queueBacklog(server, &client, 50000);
		server.disconnectClient(targetFd, "deadline test");
	}

	void onTick(Server &server, time_t) override
	{
		if (targetFd < 0)
			return;
		Client *client = server.findClientByFd(targetFd);
		/* !client: finalized, fd fully gone (expected end state).
		** !isPendingClose(): fd got reused by an unrelated, freshly-accepted
		** connection before we noticed the finalize -- pendingClose is a
		** state only our own disconnectClient() call above could have put
		** the real target into, so a client lacking it can't be it. */
		if (!client || !client->isPendingClose())
		{
			targetFd = -1;
			return;
		}
		/* Top back up, staying clear of MAX_SENDQ (64 KiB): the ceiling
		** here is 50000 + 10240 ~= 60 KB, so the refill never trips
		** isSendQExceeded() and turns this into a SendQ close instead. */
		if (client->getSendBuffer().size() < 50000)
			queueBacklog(server, client, 10000);
	}
};

/* PENDING_CLOSE_TIMEOUT (5s) is the production default; this suite injects
** a much shorter deadline via Server's constructor param so the test isn't
** stuck paying a fixed multi-second sleep every run (was 8s: 5s deadline +
** 3s test-side margin). The deadline is a whole-second time_t (matching
** _lastActivity and every other clock in Server/Client), so 1s is the
** smallest deadline that still means anything -- there is no sub-second
** granularity to shave it down to. */
static const time_t kTestPendingCloseTimeoutSec = 1;

class DeferredCloseDeadlineTest : public IrcServerTest
{
protected:
	int portBase() const override { return 17150; }

	time_t pendingCloseTimeoutSec() const override
	{
		return kTestPendingCloseTimeoutSec;
	}

	void onServerReady(Server &server) override
	{
		server.addExtension(new DeadlineRefillProbe());
	}
};

TEST_F(DeferredCloseDeadlineTest, FrozenPeerClosedByDeadlineNotDrain)
{
	/* Shrink tc's receive window well below MAX_SENDQ, BEFORE connect().
	**
	** On loopback the default tcp_rmem here is 128 KiB -- larger than the
	** 64 KiB MAX_SENDQ the probe must stay under -- so the whole payload
	** fits in the receiver's window and the server flushes it in one pass:
	** drain-complete, not the deadline, closes the connection, and no
	** payload size under MAX_SENDQ can outrun that.
	**
	** This clamp used to be applied AFTER connect(), which looks equivalent
	** and is not: the window is advertised during the handshake from the
	** buffer size in effect at that moment, and a sender may burst up to
	** the advertised window. The buffer read back as the requested 8 KiB
	** while the peer still absorbed all 50 KB, measured with SIOCOUTQ
	** showing the server's kernel send queue fully drained and ACKed. */
	TestClient tc;
	ASSERT_TRUE(tc.connect(serverPort, 4096));

	tc.sendCmd("PASS testpass");
	tc.sendCmd("NICK deadline1");
	tc.sendCmd("USER deadline1 0 * :Test");
	/* Deliberately never read: this is the frozen-peer side of the test. */

	/* Registration above is what triggers the server-side markPendingClose()
	** (synchronously, inside the probe's onClientRegistered) -- starting the
	** clock here lets us tell a deadline-driven close (~kTestPendingCloseTimeoutSec
	** later) apart from a drain-complete one (near-instant). */
	std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

	ssize_t n = -1;
	/* Detect closure WITHOUT reading a single byte.
	**
	** The previous version drained with recv() in a loop of up to 100 x 4 KB
	** per call, every 20 ms -- roughly 20 MB/s. That is the opposite of the
	** frozen peer this test is named after: the draining kept the window
	** open, so the server flushed its backlog and closed on drain-
	** completion. The comment above it said "this test never reads" while
	** the code beneath it read as fast as it could.
	**
	** poll() answers the only question that matters -- is the connection
	** gone -- and answers it without consuming the backlog that has to stay
	** unconsumed for the deadline to be reachable. Both shutdown shapes are
	** covered: the deadline's abortive close (SO_LINGER 0) raises
	** POLLERR/POLLHUP, and a graceful FIN raises POLLRDHUP. POLLERR and
	** POLLHUP are reported in revents whether or not they were requested. */
	auto checkClosed = [&]() -> bool {
		struct pollfd pfd;
		pfd.fd = tc.fd();
		pfd.events = POLLRDHUP;
		pfd.revents = 0;
		if (::poll(&pfd, 1, 0) < 0)
			return false;
		bool gone = (pfd.revents & (POLLERR | POLLHUP | POLLRDHUP)) != 0;
		n = gone ? 0 : 1;
		return gone;
	};

	bool closed = checkClosed();
	TestClient hb;
	bool hbStarted = false;

	/* checkPendingCloseTimeouts() only runs once per event-loop pass, and the
	** loop idles up to 1000ms between passes when nothing else is happening
	** -- a constant orthogonal to PENDING_CLOSE_TIMEOUT itself. hb is a
	** second, healthy connection kept busy to give the loop fd activity to
	** wake on, so passes happen at roughly this poll's own cadence instead
	** of that 1000ms idle ceiling. It's only started after a short grace
	** window (kGraceIterations) of plain polling with no hb traffic at all:
	** starting it immediately would let its own connect/register round-trip
	** (real wall-clock time) hide a near-instant drain-complete close inside
	** that setup latency, landing inside the deadline window below for the
	** wrong reason -- exactly the failure mode this test exists to catch
	** (falsified in review round 2 by shrinking the probe's payload).
	** Self-terminating (stops as soon as tc looks closed), capped so a
	** missed detection fails fast rather than hanging. */
	const int kGraceIterations = 15; // ~300ms at 20ms/iteration
	/* 200 iterations x 20ms = 4s was a fixed cap against a 1s deadline, and
	** it expired first when the whole suite ran and the event loop's passes
	** stretched under CPU contention -- the close had simply not happened
	** YET, and the test reported it as never happening. Scaled to the
	** deadline (30x, floor 200) so a loaded machine still observes it; a
	** genuinely broken sweep still fails, just later. */
	const int kMaxPolls =
		std::max(200, static_cast<int>(kTestPendingCloseTimeoutSec) * 30 * 50);
	for (int poll = 0; poll < kMaxPolls && !closed; ++poll)
	{
		if (!hbStarted && poll >= kGraceIterations)
		{
			ASSERT_TRUE(hb.connect(serverPort));
			hb.registerClient("testpass", "deadlinehb", "deadlinehb");
			hb.recvAll();
			hbStarted = true;
		}
		if (hbStarted)
			hb.sendCmd("PING :hb");
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
		closed = checkClosed();
	}

	long long elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - start).count();

	EXPECT_TRUE(closed)
		<< "Expected the frozen peer's connection to be force-closed by "
		   "the pending-close deadline, not left open indefinitely "
		   "(final recv returned " << n << ", errno=" << errno << ")";

	if (closed)
	{
		/* Pin the closure to the deadline itself, not just "eventually
		** closed" -- a drain-complete close (or a broken deadline that
		** happens to get cleaned up some other way) would land far outside
		** this window. */
		const long long kDeadlineMs = static_cast<long long>(kTestPendingCloseTimeoutSec) * 1000;
		EXPECT_GE(elapsedMs, kDeadlineMs / 2)
			<< "Closed too fast (" << elapsedMs << "ms) for a " << kDeadlineMs
			<< "ms injected deadline -- looks like drain-completion, not "
			   "the pending-close deadline sweep";
		/* The lower bound above is the real property: it separates a
		** deadline-driven close from a drain-complete one. The upper bound is
		** only a sanity check, and a tight one was FLAKY -- when a second test
		** binary runs alongside this one, the event loop's passes stretch
		** under CPU contention and the close lands past 3x the deadline
		** without anything being wrong. The poll loop above already caps at
		** 200 x 20ms, so a close that never happens fails on `closed` instead.
		** This bound therefore only has to catch a close that is late by
		** orders of magnitude, not one that is late by scheduling jitter. */
		EXPECT_LE(elapsedMs, kDeadlineMs * 10)
			<< "Closed suspiciously late (" << elapsedMs << "ms) for a "
			<< kDeadlineMs << "ms injected deadline";
	}
}
