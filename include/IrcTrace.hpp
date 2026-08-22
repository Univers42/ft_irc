#ifndef IRCTRACE_HPP
#define IRCTRACE_HPP

#include <string>

namespace IrcTrace {
void inbound(int fd, const std::string& peer, const std::string& line);

void outbound(int fd, const std::string& peer, const std::string& line);

void sessionOpen(int fd, const std::string& host);
void sessionRegistered(int fd, const std::string& prefix);
void sessionClose(int fd, const std::string& peer, const std::string& reason);

std::string summary();

std::string numericName(const std::string& numeric);

}  // namespace IrcTrace

#endif
