/* ─── Shared integration-test harness: TCP TestClient + Server fixture ───
 *
 * Extracted from test_integration.cpp so every protocol-level suite
 * (integration, security, file transfer) reuses one client and one fixture.
 * Each suite subclasses IrcServerTest with its own port base to avoid
 * cross-suite bind clashes.
 */

#ifndef TEST_HARNESS_HPP
#define TEST_HARNESS_HPP

#include <gtest/gtest.h>
#include "Limits.hpp"
#include "Server.hpp"
#include "ext/RegisterExtensions.hpp"

#include <thread>
#include <chrono>
#include <cstring>
#include <cerrno>
#include <vector>
#include <string>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/types.h>

/* ══════════════════════════════════════════════════════════════════════════
 * TestClient — lightweight TCP client for protocol-level testing
 * ══════════════════════════════════════════════════════════════════════ */

class TestClient
{
public:
	TestClient() : _fd(-1) {}
	~TestClient() { disconnect(); }

	/* rcvBufBytes > 0 clamps SO_RCVBUF *before* connect().
	**
	** The ordering is the whole point. Setting SO_RCVBUF after connect()
	** does resize the buffer, but the receive window was already advertised
	** during the handshake from the default (128 KiB here), and a sender may
	** burst up to that window -- so a "frozen" peer still absorbs ~64 KiB it
	** was never supposed to have room for. Clamping before connect() is what
	** makes the small window real on the wire.
	**
	** Only backpressure tests need this; everyone else calls connect(port). */
	bool connect(int port, int rcvBufBytes = 0)
	{
		_fd = socket(AF_INET, SOCK_STREAM, 0);
		if (_fd < 0) return false;

		if (rcvBufBytes > 0)
			setsockopt(_fd, SOL_SOCKET, SO_RCVBUF, &rcvBufBytes,
					   sizeof(rcvBufBytes));

		struct sockaddr_in addr;
		std::memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);
		inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

		if (::connect(_fd, reinterpret_cast<struct sockaddr *>(&addr),
					  sizeof(addr)) < 0)
		{
			close(_fd);
			_fd = -1;
			return false;
		}

		/* Set a read timeout so tests don't hang */
		struct timeval tv;
		tv.tv_sec = 2;
		tv.tv_usec = 0;
		setsockopt(_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
		return true;
	}

	void disconnect()
	{
		if (_fd >= 0)
		{
			close(_fd);
			_fd = -1;
		}
	}

	int fd() const { return _fd; }

	void sendRaw(const std::string &data)
	{
		if (_fd >= 0)
			send(_fd, data.c_str(), data.size(), 0);
	}

	void sendCmd(const std::string &cmd)
	{
		sendRaw(cmd + "\r\n");
	}

	std::string recvAll(int timeoutMs = 500)
	{
		std::string result;
		char buf[4096];

		/* Set shorter timeout for bulk reads */
		struct timeval tv;
		tv.tv_sec = 0;
		tv.tv_usec = timeoutMs * 1000;
		setsockopt(_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

		while (true)
		{
			ssize_t n = recv(_fd, buf, sizeof(buf) - 1, 0);
			if (n <= 0) break;
			buf[n] = '\0';
			result += buf;
		}
		return result;
	}

	bool registerClient(const std::string &pass, const std::string &nick,
						const std::string &user)
	{
		sendCmd("PASS " + pass);
		sendCmd("NICK " + nick);
		sendCmd("USER " + user + " 0 * :Test User");
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
		return true;
	}

	bool hasNumeric(const std::string &data, const std::string &numeric)
	{
		return data.find(" " + numeric + " ") != std::string::npos;
	}

private:
	int _fd;
};

/* ══════════════════════════════════════════════════════════════════════════
 * IrcServerTest — fixture running a Server in a background thread.
 * Subclass and override portBase() per suite.
 * ══════════════════════════════════════════════════════════════════════ */

class IrcServerTest : public ::testing::Test
{
protected:
	virtual int portBase() const { return 17100; }
	/* Override to act on the server after construction, before run(). */
	virtual void onServerReady(Server &server) { (void)server; }
	/* Override to shrink the pending-close deadline sweep below its 5s
	** production default (Server's ctor takes it directly now) -- avoids
	** paying real wall-clock seconds in tests that need to observe it. */
	virtual time_t pendingCloseTimeoutSec() const { return Limits::kPendingCloseTimeout; }

	void SetUp() override
	{
		server = NULL;
		serverPort = 0;

		/* Two test binaries running at once (a CI matrix, or a second agent
		** driving the suite) used to scan the SAME range and fight over it.
		**
		** Shift every suite by ONE per-process band rather than per-suite:
		** the bases are only 50-100 apart, so a per-suite offset would push
		** one suite onto another suite's base. A 1000-wide band keeps the
		** existing relative layout intact and just moves it out of the way. */
		const int base = portBase() + processPortBand();

		for (int port = base; port < base + 100; ++port)
		{
			try
			{
				server = new Server(port, "testpass", pendingCloseTimeoutSec());
				serverPort = port;
				break;
			}
			catch (...) { continue; }
		}
		ASSERT_NE(server, nullptr) << "Could not bind to any port";

		/* Tests link the full tier's registration TU */
		registerExtensions(*server);

		/* Suite-specific setup (e.g. extra extensions) before run() */
		onServerReady(*server);

		/* Run server in a background thread */
		serverThread = std::thread([this]() {
			server->run();
		});

		/* Deliberately a plain sleep, not a connect-probe. bind() and listen()
		** already ran in the Server constructor, so the kernel queues
		** connections before run() ever calls accept() -- a probe would
		** therefore succeed instantly without proving anything, and its
		** connect/close would land on the server as a PHANTOM CLIENT,
		** inflating every extension's connect/disconnect counts. The real
		** cause of the intermittent failures was port collision between
		** concurrent test processes, which processPortBand() fixes. */
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	/* One band per process, stable for the process's lifetime. Bases span
	** 17100-17800, so 1000 is wide enough that bands cannot overlap. */
	static int processPortBand()
	{
		static const int band =
			static_cast<int>(::getpid() % 20) * 1000;
		return band;
	}

	void TearDown() override
	{
		if (server)
		{
			Server::isRunning = false;
			if (serverThread.joinable())
				serverThread.join();
			delete server;
			server = NULL;
			Server::isRunning = true;
		}
	}

	Server *server;
	int serverPort;
	std::thread serverThread;
};

#endif /* TEST_HARNESS_HPP */
