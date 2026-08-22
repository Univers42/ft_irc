#ifndef IRCTRACE_HPP
#define IRCTRACE_HPP

#include <string>

/*
** IrcTrace — the server-side protocol trace.
**
** Renders every line that crosses the socket boundary, in the exact RFC 2812
** syntax it travels in, so the console shows the real conversation between
** the server and its clients rather than a paraphrase of it:
**
**   19:04:11.882  fd 6  <<  NICK alice                            NICK
**   19:04:11.883  fd 6  >>  :ft_irc 001 alice :Welcome to the …   001 RPL_WELCOME
**
** Two choke points feed it, and there are only two because the server has
** only two: Server::handleMessage() for everything a client sends, and
** Client::queueMessage() for everything the server sends — the single point
** every reply, relay and broadcast funnels through (see its comment).
** Tracing there means no call site can bypass the log.
**
** Credentials are redacted (PASS, the +k key, JOIN keys). A console log is
** routinely pasted into a bug report or a defense; printing the server
** password there would be a real leak, and the trace is worth nothing if it
** cannot be shown to anyone.
**
** Everything here is a no-op unless the level allows it — see Log::enabled().
** This is kernel code: it links into every tier and depends only on libcpp's
** str module, which every tier already compiles.
*/
namespace IrcTrace {

/* A line arriving from a client, already sanitized and framed. */
void inbound(int fd, const std::string& peer, const std::string& line);

/* A line queued towards a client, exactly as it will hit the wire. */
void outbound(int fd, const std::string& peer, const std::string& line);

/* Session lifecycle. `detail` is free text (host:port, quit reason, …). */
void sessionOpen(int fd, const std::string& host);
void sessionRegistered(int fd, const std::string& prefix);
void sessionClose(int fd, const std::string& peer, const std::string& reason);

/* Totals since start, for the shutdown line. */
std::string summary();

/* Human name of an IRC numeric ("001" -> "RPL_WELCOME"); empty if unknown. */
std::string numericName(const std::string& numeric);

}  // namespace IrcTrace

#endif /* IRCTRACE_HPP */
