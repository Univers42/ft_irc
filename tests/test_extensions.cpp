/* ─── Extension seam tests: hook firing, interception, ownership ─── */

#include "PostMan.hpp"
#include "TestHarness.hpp"
#include "ext/IServerExtension.hpp"

#include <atomic>

/* A probe extension recording every hook invocation. */
struct ProbeExt : public IServerExtension
{
	static std::atomic<int> destroyed;

	std::atomic<int> started, registered, disconnected, joins, parts;
	std::atomic<int> commands, privmsgs, audits, ticks;

	ProbeExt()
		: started(0), registered(0), disconnected(0), joins(0), parts(0),
		  commands(0), privmsgs(0), audits(0), ticks(0) {}
	~ProbeExt() { ++destroyed; }

	const char *name() const override { return "probe"; }

	void onServerStart(Server &) override { ++started; }
	void onTick(Server &, time_t) override { ++ticks; }
	void onClientRegistered(Server &, Client &) override { ++registered; }
	void onClientDisconnect(Server &, Client &, const std::string &) override
	{ ++disconnected; }
	void onJoin(Server &, Client &, Channel &) override { ++joins; }
	void onPart(Server &, Client &, Channel &) override { ++parts; }

	bool onCommand(Server &server, Client &client, const Message &msg) override
	{
		if (msg.command != "PROBE")
			return false;
		++commands;
		server.sendToClient(&client, "NOTICE " + client.getNickname()
							+ " :probe-ack");
		return true;
	}

	bool onPrivmsg(Server &, Client &, const std::string &target,
				   const std::string &) override
	{
		if (target != "probe")
			return false;
		++privmsgs;
		return true;
	}

	bool reservesNick(const std::string &nick) const override
	{
		return nick == "probe";
	}

	void onAudit(const std::string &, const std::string &,
				 const std::string &) override
	{ ++audits; }
};

std::atomic<int> ProbeExt::destroyed(0);

/* Fixture: like IrcServerTest, but installs the probe before run(). */
class ExtensionTest : public IrcServerTest
{
protected:
	int portBase() const override { return 17600; }

	void SetUp() override
	{
		probe = new ProbeExt();
		IrcServerTest::SetUp();
	}

	/* hook between construction and thread start */
	void onServerReady(Server &s) override
	{
		s.addExtension(probe);
	}

	ProbeExt *probe; /* owned by the Server */
};

TEST_F(ExtensionTest, LifecycleAndChannelHooksFire)
{
	EXPECT_EQ(probe->started.load(), 1) << "onServerStart fires once";

	TestClient tc;
	ASSERT_TRUE(tc.connect(serverPort));
	tc.registerClient("testpass", "extuser", "extuser");
	tc.sendCmd("JOIN #ext");
	tc.sendCmd("PART #ext");
	tc.sendCmd("QUIT :done");
	std::this_thread::sleep_for(std::chrono::milliseconds(400));

	EXPECT_EQ(probe->registered.load(), 1);
	EXPECT_EQ(probe->joins.load(), 1);
	EXPECT_EQ(probe->parts.load(), 1);
	EXPECT_EQ(probe->disconnected.load(), 1);
	EXPECT_GE(probe->audits.load(), 3) << "register+join+part audited";
	EXPECT_GE(probe->ticks.load(), 1) << "onTick fires each loop pass";
}

TEST_F(ExtensionTest, OnCommandConsumesUnknownCommandOnly)
{
	TestClient tc;
	ASSERT_TRUE(tc.connect(serverPort));
	tc.registerClient("testpass", "extcmd", "extcmd");
	tc.recvAll();

	tc.sendCmd("PROBE hello");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	std::string out = tc.recvAll();
	EXPECT_NE(out.find("probe-ack"), std::string::npos) << out;
	EXPECT_EQ(out.find(" 421 "), std::string::npos)
		<< "handled command must not 421";
	EXPECT_EQ(probe->commands.load(), 1);

	/* A genuinely unknown command still 421s (extension returned false) */
	tc.sendCmd("NOSUCHCMD");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	out = tc.recvAll();
	EXPECT_NE(out.find(" 421 "), std::string::npos) << out;
}

TEST_F(ExtensionTest, OnPrivmsgInterceptsAndReservesNick)
{
	TestClient tc;
	ASSERT_TRUE(tc.connect(serverPort));
	tc.registerClient("testpass", "extpm", "extpm");
	tc.recvAll();

	/* claimed target: no ERR_NOSUCHNICK even though no such client */
	tc.sendCmd("PRIVMSG probe :are you there");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	std::string out = tc.recvAll();
	EXPECT_EQ(out.find(" 401 "), std::string::npos)
		<< "claimed PRIVMSG target must not 401: " << out;
	EXPECT_EQ(probe->privmsgs.load(), 1);

	/* reserved nick collides */
	tc.sendCmd("NICK probe");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	out = tc.recvAll();
	EXPECT_NE(out.find(" 433 "), std::string::npos) << out;
}

TEST_F(ExtensionTest, ServerOwnsAndDeletesExtensions)
{
	int before = ProbeExt::destroyed.load();
	/* TearDown deletes the server, which must delete the probe. */
	TearDown();
	EXPECT_EQ(ProbeExt::destroyed.load(), before + 1)
		<< "Server must delete registered extensions";
}
