#include <csignal>
#include <cstdlib>
#include <string>

#include "IrcTrace.hpp"
#include "Log.hpp"
#include "Server.hpp"
#include "ext/RegisterExtensions.hpp"
#include "libcpp/str/format.hpp"

static void signalHandler(int signum) {
  (void)signum;
  Server::isRunning = false;
}

int main(int argc, char** argv) {
  Log::configureFromEnv();

  if (argc != 3) {
    Log::error("usage: ./ircserv <port> <password>");
    return 1;
  }

  std::string portStr = argv[1];
  std::string password = argv[2];

  long port = 0;
  if (!libcpp::str::parse_long(portStr, 1, 65535, port)) {
    Log::error("port must be a number between 1 and 65535");
    return 1;
  }

  if (password.empty()) {
    Log::error("password cannot be empty");
    return 1;
  }

  signal(SIGPIPE, SIG_IGN);
  signal(SIGINT, signalHandler);
  signal(SIGTERM, signalHandler);

  try {
    Server server(static_cast<int>(port), password);
    registerExtensions(server);
    server.run();
    Log::info("traffic: " + IrcTrace::summary());
    Log::info("shutting down — server stopped cleanly");
  } catch (const std::exception& e) {
    Log::error(std::string("fatal: ") + e.what());
    Log::setSink(NULL);
    return 1;
  }

  Log::setSink(NULL);
  return 0;
}
