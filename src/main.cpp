#include "Log.hpp"
#include "Server.hpp"
#include "ext/RegisterExtensions.hpp"
#include "libcpp/str/format.hpp"

#include <csignal>
#include <cstdlib>

// Async-signal-safe: only flip the flag. The run() loop notices it and
// returns; the (non-signal-safe, pretty) shutdown line is printed from
// main() afterwards, through the Log writer.
static void signalHandler(int signum) {
  (void)signum;
  Server::isRunning = false;
}

int main(int argc, char** argv) {
  if (argc != 3) {
    Log::error("usage: ./ircserv <port> <password>");
    return 1;
  }

  std::string portStr = argv[1];
  std::string password = argv[2];

  /* One strict, range-checked parse. The previous digits-check + atoi()
  ** pair let an in-range-looking but overflowing port ("99999999999")
  ** through to atoi, whose result on overflow is undefined. */
  long port = 0;
  if (!libcpp::str::parse_long(portStr, 1, 65535, port)) {
    Log::error("port must be a number between 1 and 65535");
    return 1;
  }

  if (password.empty()) {
    Log::error("password cannot be empty");
    return 1;
  }

  // Ignore SIGPIPE (critical — send() to closed socket)
  signal(SIGPIPE, SIG_IGN);
  signal(SIGINT, signalHandler);
  signal(SIGTERM, signalHandler);

  try {
    Server server(static_cast<int>(port), password);
    registerExtensions(server); /* which set depends on the build tier */
    server.run();
    Log::info("shutting down — server stopped cleanly");
  } catch (const std::exception& e) {
    Log::error(std::string("fatal: ") + e.what());
    Log::setSink(NULL); /* free any installed log sink before exit */
    return 1;
  }

  Log::setSink(
      NULL); /* free any installed log sink — nothing left in use at exit */
  return 0;
}
