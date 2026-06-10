#include "Server.hpp"
#include "Log.hpp"
#include "ext/RegisterExtensions.hpp"

#include <cctype>
#include <cstdlib>
#include <csignal>

// Async-signal-safe: only flip the flag. The run() loop notices it and
// returns; the (non-signal-safe, pretty) shutdown line is printed from
// main() afterwards, through the Log writer.
static void signalHandler(int signum)
{
	(void)signum;
	Server::isRunning = false;
}

static bool isNumber(const std::string &str)
{
	if (str.empty())
		return false;
	for (size_t i = 0; i < str.size(); ++i)
	{
		if (!std::isdigit(static_cast<unsigned char>(str[i])))
			return false;
	}
	return true;
}

int main(int argc, char **argv)
{
	if (argc != 3)
	{
		Log::error("usage: ./ircserv <port> <password>");
		return 1;
	}

	std::string portStr = argv[1];
	std::string password = argv[2];

	if (!isNumber(portStr))
	{
		Log::error("port must be a number");
		return 1;
	}

	int port = std::atoi(portStr.c_str());
	if (port < 1 || port > 65535)
	{
		Log::error("port must be between 1 and 65535");
		return 1;
	}

	if (password.empty())
	{
		Log::error("password cannot be empty");
		return 1;
	}

	// Ignore SIGPIPE (critical — send() to closed socket)
	signal(SIGPIPE, SIG_IGN);
	signal(SIGINT, signalHandler);
	signal(SIGTERM, signalHandler);

	try
	{
		Server server(port, password);
		registerExtensions(server); /* which set depends on the build tier */
		server.run();
		Log::info("shutting down — server stopped cleanly");
	}
	catch (const std::exception &e)
	{
		Log::error(std::string("fatal: ") + e.what());
		Log::setSink(NULL); /* free any installed log sink before exit */
		return 1;
	}

	Log::setSink(NULL); /* free any installed log sink — nothing left in use at exit */
	return 0;
}
