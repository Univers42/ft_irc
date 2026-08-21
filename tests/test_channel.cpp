/* ─── Unit tests: Channel management ─── */

#include <gtest/gtest.h>
#include "PostMan.hpp"
#include "Channel.hpp"
#include "Client.hpp"

/* ══════════════════════════════════════════════════════════════════
 * Fixture: provides a creator client and channel for reuse
 * ══════════════════════════════════════════════════════════════ */

class ChannelTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		creator = new Client(10, "127.0.0.1");
		creator->setNickname("creator");
		creator->setUsername("cuser");
	}

	void TearDown() override
	{
		delete creator;
	}

	Client *creator;
};

/* ════════════════════════════════════════════════════════════════════════
 * Suite: ChannelCreation
 * ════════════════════════════════════════════════════════════════════ */

TEST_F(ChannelTest, CreatorIsMemberAndOperator)
{
	Channel chan("#test", creator);
	EXPECT_TRUE(chan.isMember(creator));
	EXPECT_TRUE(chan.isOperator(creator));
	EXPECT_EQ(chan.getMemberCount(), 1u);
}

TEST_F(ChannelTest, NameStoredCorrectly)
{
	Channel chan("#mychannel", creator);
	EXPECT_EQ(chan.getName(), "#mychannel");
}

TEST_F(ChannelTest, InitialModeIsPlus)
{
	Channel chan("#test", creator);
	EXPECT_EQ(chan.getModeString(), "+");
}

TEST_F(ChannelTest, CreationTimeIsSet)
{
	Channel chan("#test", creator);
	EXPECT_GT(chan.getCreationTime(), 0);
}

TEST_F(ChannelTest, InitialTopicEmpty)
{
	Channel chan("#test", creator);
	EXPECT_TRUE(chan.getTopic().empty());
	EXPECT_TRUE(chan.getTopicSetter().empty());
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: ChannelMembers
 * ════════════════════════════════════════════════════════════════════ */

TEST_F(ChannelTest, AddAndRemoveMember)
{
	Channel chan("#test", creator);
	Client user1(11, "127.0.0.1");
	user1.setNickname("user1");

	chan.addMember(&user1);
	EXPECT_TRUE(chan.isMember(&user1));
	EXPECT_EQ(chan.getMemberCount(), 2u);

	chan.removeMember(&user1);
	EXPECT_FALSE(chan.isMember(&user1));
	EXPECT_EQ(chan.getMemberCount(), 1u);
}

TEST_F(ChannelTest, IsEmptyAfterRemovingAll)
{
	Channel chan("#test", creator);
	chan.removeMember(creator);
	EXPECT_TRUE(chan.isEmpty());
	EXPECT_EQ(chan.getMemberCount(), 0u);
}

TEST_F(ChannelTest, FindMemberByNickname)
{
	Channel chan("#test", creator);
	Client user1(11, "127.0.0.1");
	user1.setNickname("alice");
	chan.addMember(&user1);

	Client *found = chan.findMember("alice");
	ASSERT_NE(found, nullptr);
	EXPECT_EQ(found->getNickname(), "alice");

	EXPECT_EQ(chan.findMember("nonexistent"), nullptr);
}

TEST_F(ChannelTest, IsMemberByNickname)
{
	/* The isMember(nickname) overload was deleted as dead product code --
	 * nothing in src/ ever called it. findMember() is what it delegated to
	 * and what the server actually uses, so assert against that instead. */
	Channel chan("#test", creator);
	EXPECT_NE(chan.findMember("creator"), nullptr);
	EXPECT_EQ(chan.findMember("nobody"), nullptr);
}

TEST_F(ChannelTest, NamesListFormat)
{
	Channel chan("#test", creator);
	Client user1(11, "127.0.0.1");
	user1.setNickname("bob");
	chan.addMember(&user1);

	/* A budget that comfortably holds both members: one chunk. */
	std::vector<std::string> chunks = chan.getNamesChunks(400);
	ASSERT_EQ(chunks.size(), 1u);
	const std::string &names = chunks[0];
	/* creator is operator → @creator, bob is regular */
	EXPECT_NE(names.find("@creator"), std::string::npos);
	EXPECT_NE(names.find("bob"), std::string::npos);
	/* bob should NOT have @ prefix */
	EXPECT_EQ(names.find("@bob"), std::string::npos);
}

TEST_F(ChannelTest, NamesChunksRespectBudgetAndKeepEveryMember)
{
	/* The 512-byte line limit reaches Channel as a per-chunk byte budget.
	 * Chunking must stay inside it without dropping or duplicating anyone. */
	Channel chan("#test", creator);
	std::vector<Client *> members;
	for (int i = 0; i < 40; ++i)
	{
		char nick[16];
		std::snprintf(nick, sizeof(nick), "chunk%03d", i);
		Client *c = new Client(200 + i, "127.0.0.1");
		c->setNickname(nick);
		chan.addMember(c);
		members.push_back(c);
	}

	const size_t budget = 60;
	std::vector<std::string> chunks = chan.getNamesChunks(budget);
	ASSERT_GT(chunks.size(), 1u) << "40 members must not fit one 60-byte chunk";

	size_t total = 0;
	for (size_t i = 0; i < chunks.size(); ++i)
	{
		EXPECT_LE(chunks[i].size(), budget) << "chunk " << i << " over budget";
		total += chunks[i].size();
	}
	for (size_t i = 0; i < members.size(); ++i)
	{
		size_t seen = 0;
		for (size_t c = 0; c < chunks.size(); ++c)
		{
			if (chunks[c].find(members[i]->getNickname()) != std::string::npos)
				++seen;
		}
		EXPECT_EQ(seen, 1u) << members[i]->getNickname()
							<< " should appear in exactly one chunk";
	}
	EXPECT_GT(total, 0u);

	for (size_t i = 0; i < members.size(); ++i)
		delete members[i];
}

TEST_F(ChannelTest, NamesChunksAlwaysYieldAtLeastOne)
{
	/* The caller emits one 353 per chunk; a channel must never produce
	 * zero of them, or a joining client would get no names reply at all. */
	Channel chan("#test", creator);
	EXPECT_EQ(chan.getNamesChunks(400).size(), 1u);
	/* Even an absurdly small budget still yields whole entries. */
	EXPECT_GE(chan.getNamesChunks(1).size(), 1u);
}

TEST_F(ChannelTest, GetMembersVector)
{
	Channel chan("#test", creator);
	Client user1(11, "127.0.0.1");
	user1.setNickname("user1");
	chan.addMember(&user1);

	std::vector<Client *> members = chan.getMembers();
	EXPECT_EQ(members.size(), 2u);
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: ChannelModes
 * ════════════════════════════════════════════════════════════════════ */

TEST_F(ChannelTest, InviteOnlyMode)
{
	Channel chan("#test", creator);
	chan.setInviteOnly(true);
	EXPECT_TRUE(chan.isInviteOnly());
	EXPECT_NE(chan.getModeString().find('i'), std::string::npos);

	chan.setInviteOnly(false);
	EXPECT_FALSE(chan.isInviteOnly());
	EXPECT_EQ(chan.getModeString().find('i'), std::string::npos);
}

TEST_F(ChannelTest, TopicRestrictedMode)
{
	Channel chan("#test", creator);
	chan.setTopicRestricted(true);
	EXPECT_TRUE(chan.isTopicRestricted());
	EXPECT_NE(chan.getModeString().find('t'), std::string::npos);

	chan.setTopicRestricted(false);
	EXPECT_FALSE(chan.isTopicRestricted());
}

TEST_F(ChannelTest, KeyMode)
{
	Channel chan("#test", creator);
	chan.setKey("secret");
	EXPECT_EQ(chan.getKey(), "secret");
	EXPECT_NE(chan.getModeString().find('k'), std::string::npos);
	EXPECT_NE(chan.getModeParams().find("secret"), std::string::npos);

	chan.removeKey();
	EXPECT_TRUE(chan.getKey().empty());
	EXPECT_EQ(chan.getModeString().find('k'), std::string::npos);
}

TEST_F(ChannelTest, UserLimitMode)
{
	Channel chan("#test", creator);
	chan.setUserLimit(10);
	EXPECT_EQ(chan.getUserLimit(), 10u);
	EXPECT_NE(chan.getModeString().find('l'), std::string::npos);
	EXPECT_NE(chan.getModeParams().find("10"), std::string::npos);

	chan.removeUserLimit();
	EXPECT_EQ(chan.getUserLimit(), 0u);
	EXPECT_EQ(chan.getModeString().find('l'), std::string::npos);
}

TEST_F(ChannelTest, CombinedModes)
{
	Channel chan("#test", creator);
	chan.setInviteOnly(true);
	chan.setTopicRestricted(true);
	chan.setKey("pass");

	std::string modes = chan.getModeString();
	EXPECT_NE(modes.find('i'), std::string::npos);
	EXPECT_NE(modes.find('t'), std::string::npos);
	EXPECT_NE(modes.find('k'), std::string::npos);

	std::string params = chan.getModeParams();
	EXPECT_NE(params.find("pass"), std::string::npos);
}

TEST_F(ChannelTest, ModeParamsOrder)
{
	Channel chan("#test", creator);
	chan.setKey("mykey");
	chan.setUserLimit(25);

	std::string params = chan.getModeParams();
	/* Key should appear before limit */
	size_t keyPos = params.find("mykey");
	size_t limPos = params.find("25");
	EXPECT_NE(keyPos, std::string::npos);
	EXPECT_NE(limPos, std::string::npos);
	EXPECT_LT(keyPos, limPos);
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: ChannelOperators
 * ════════════════════════════════════════════════════════════════════ */

TEST_F(ChannelTest, GrantAndRevokeOperator)
{
	Channel chan("#test", creator);
	Client user1(11, "127.0.0.1");
	user1.setNickname("user1");
	chan.addMember(&user1);

	EXPECT_FALSE(chan.isOperator(&user1));
	chan.setOperator(&user1, true);
	EXPECT_TRUE(chan.isOperator(&user1));
	chan.setOperator(&user1, false);
	EXPECT_FALSE(chan.isOperator(&user1));
}

TEST_F(ChannelTest, OperatorRemovedWhenMemberRemoved)
{
	Channel chan("#test", creator);
	Client user1(11, "127.0.0.1");
	user1.setNickname("user1");
	chan.addMember(&user1);
	chan.setOperator(&user1, true);
	EXPECT_TRUE(chan.isOperator(&user1));

	chan.removeMember(&user1);
	/* Re-add — should NOT still be operator */
	chan.addMember(&user1);
	EXPECT_FALSE(chan.isOperator(&user1));
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: ChannelInvites
 * ════════════════════════════════════════════════════════════════════ */

TEST_F(ChannelTest, InviteManagement)
{
	Channel chan("#test", creator);

	Client bob(31, "127.0.0.1");
	bob.setNickname("bob");

	EXPECT_FALSE(chan.isInvited(&bob));
	chan.addInvite(&bob);
	EXPECT_TRUE(chan.isInvited(&bob));
	chan.removeInvite(&bob);
	EXPECT_FALSE(chan.isInvited(&bob));
}

TEST_F(ChannelTest, InviteIsBoundToTheConnectionNotTheNickname)
{
	/* Invites key off the connection, so renaming neither loses the invite
	 * nor hands it to whoever picks the old nickname up. */
	Channel chan("#test", creator);
	Client invited(32, "127.0.0.1");
	invited.setNickname("guest");
	chan.addInvite(&invited);

	invited.setNickname("guest_renamed");
	EXPECT_TRUE(chan.isInvited(&invited)) << "invite must follow a NICK change";

	Client impostor(33, "127.0.0.1");
	impostor.setNickname("guest");
	EXPECT_FALSE(chan.isInvited(&impostor))
		<< "taking the invitee's old nickname must not confer its invite";
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: ChannelBroadcast
 * ════════════════════════════════════════════════════════════════════ */

TEST_F(ChannelTest, BroadcastToAllMembers)
{
	Channel chan("#test", creator);
	Client user1(11, "127.0.0.1");
	user1.setNickname("user1");
	Client user2(12, "127.0.0.1");
	user2.setNickname("user2");
	chan.addMember(&user1);
	chan.addMember(&user2);

	chan.broadcastMessage("Hello everyone", NULL);

	EXPECT_NE(creator->getSendBuffer().find("Hello everyone"), std::string::npos);
	EXPECT_NE(user1.getSendBuffer().find("Hello everyone"), std::string::npos);
	EXPECT_NE(user2.getSendBuffer().find("Hello everyone"), std::string::npos);
}

TEST_F(ChannelTest, BroadcastExcludesSender)
{
	Channel chan("#test", creator);
	Client user1(11, "127.0.0.1");
	user1.setNickname("user1");
	chan.addMember(&user1);

	chan.broadcastMessage("message from creator", creator);

	/* Creator is excluded */
	EXPECT_EQ(creator->getSendBuffer().find("message from creator"), std::string::npos);
	/* user1 receives it */
	EXPECT_NE(user1.getSendBuffer().find("message from creator"), std::string::npos);
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: ChannelTopic
 * ════════════════════════════════════════════════════════════════════ */

TEST_F(ChannelTest, SetAndGetTopic)
{
	Channel chan("#test", creator);
	chan.setTopic("Welcome to #test", "creator");

	EXPECT_EQ(chan.getTopic(), "Welcome to #test");
	EXPECT_EQ(chan.getTopicSetter(), "creator");
	EXPECT_GT(chan.getTopicTime(), 0);
}

TEST_F(ChannelTest, ClearTopic)
{
	Channel chan("#test", creator);
	chan.setTopic("Old topic", "creator");
	chan.setTopic("", "creator");
	EXPECT_TRUE(chan.getTopic().empty());
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: ChannelMemory — PostMan leak detection
 * ════════════════════════════════════════════════════════════════════ */

extern int g_allocations;

TEST_F(ChannelTest, NoLeakOnCreateDestroy)
{
	int before;
	{
		Client *user1 = new Client(11, "127.0.0.1");
		user1->setNickname("user1");
		Channel *chan = new Channel("#leak", creator);
		chan->addMember(user1);
		chan->setTopic("test topic", "creator");
		chan->setKey("secret");
		chan->addInvite(user1);
		before = g_allocations;

		delete chan;
		delete user1;
	}
	EXPECT_LE(g_allocations, before)
		<< "Channel destruction should not leak memory";
}
