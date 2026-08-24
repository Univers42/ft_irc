#ifndef IRCTRACE_HPP
#define IRCTRACE_HPP

#include <string>
#include <vector>

namespace IrcTrace {
void inbound(int fd, const std::string& peer, const std::string& line);

void outbound(int fd, const std::string& peer, const std::string& line);

void sessionOpen(int fd, const std::string& host);
void sessionRegistered(int fd, const std::string& prefix);
void sessionClose(int fd, const std::string& peer, const std::string& reason);

std::string summary();

std::string numericName(const std::string& numeric);

/*
** Index into `params` of the one parameter that must never reach a log, or
** -1 when this command carries no secret. `params` is the list AFTER the
** command, matching Message::params.
**
** This is the single statement of that policy. Both renderers consult it:
** IrcTrace::redact() for the raw wire line, and operator<<(ostream&,
** Message) for the parsed form. Keeping one copy is the point — the parsed
** renderer used to have no redaction at all, so `Log::trace() << msg` in
** Server::handleLine printed PASS passwords and channel keys in full while
** the wire trace right above it showed them as ***.
*/
int secretParamIndex(const std::string& command, const std::vector<std::string>& params);

}  // namespace IrcTrace

#endif
