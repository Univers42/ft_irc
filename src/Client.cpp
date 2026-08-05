#include "Client.hpp"
#include "Replies.hpp"

Client::Client(int fd, const std::string& hostname)
    : _fd(fd),
      _nickname("*"),
      _hostname(hostname),
      _registered(false),
      _passSent(false),
      _nickSet(false),
      _userSet(false),
      _io(MAX_MSGLEN, MAX_SENDQ),
      _lastActivity(std::time(NULL)),
      _pingSent(false),
      _pendingClose(false),
      _pendingCloseSince(0),
      _tearingDown(false) {}

Client::~Client() {}

/* ─── Getters ─── */

int Client::getFd() const { return _fd; }
const std::string& Client::getNickname() const { return _nickname; }
const std::string& Client::getUsername() const { return _username; }
const std::string& Client::getRealname() const { return _realname; }
const std::string& Client::getHostname() const { return _hostname; }
const std::string& Client::getPassword() const { return _password; }
bool Client::isRegistered() const { return _registered; }
bool Client::hasPassSent() const { return _passSent; }
bool Client::hasNick() const { return _nickSet; }
bool Client::hasUser() const { return _userSet; }
time_t Client::getLastActivity() const { return _lastActivity; }
bool Client::isPingSent() const { return _pingSent; }
bool Client::isPendingClose() const { return _pendingClose; }
time_t Client::getPendingCloseSince() const { return _pendingCloseSince; }
bool Client::isTearingDown() const { return _tearingDown; }

std::string Client::getPrefix() const {
  return _nickname + "!" + _username + "@" + _hostname;
}

/* ─── Setters ─── */

void Client::setNickname(const std::string& nickname) { _nickname = nickname; }
void Client::setUsername(const std::string& username) { _username = username; }
void Client::setRealname(const std::string& realname) { _realname = realname; }
void Client::setPassword(const std::string& password) { _password = password; }
void Client::setRegistered(bool reg) { _registered = reg; }
void Client::setPassSent(bool sent) { _passSent = sent; }
void Client::setNickSet(bool set) { _nickSet = set; }
void Client::setUserSet(bool set) { _userSet = set; }
void Client::updateLastActivity() { _lastActivity = std::time(NULL); }
void Client::setPingSent(bool sent) { _pingSent = sent; }

void Client::markPendingClose() {
  _pendingClose = true;
  _pendingCloseSince = std::time(NULL);
}

void Client::markTearingDown() { _tearingDown = true; }

/* ─── Buffer management ─── */

void Client::appendToRecvBuffer(const std::string& data) { _io.feed(data); }

/* One choke point sanitizes every extracted line: stray CR and NUL bytes are
** stripped so no parameter can ever smuggle a second IRC line ("a\rQUIT") or
** truncate C-string views downstream.  \x01 (CTCP/DCC) passes untouched. */
static std::string sanitizeLine(const std::string& line) {
  std::string out;
  out.reserve(line.size());
  for (std::string::size_type i = 0; i < line.size(); ++i) {
    if (line[i] != '\r' && line[i] != '\0') out += line[i];
  }
  return out;
}

std::vector<std::string> Client::extractMessages() {
  std::vector<std::string> messages;
  std::string line;

  while (_io.nextLine(line)) {
    line = sanitizeLine(line);
    if (!line.empty()) messages.push_back(line);
  }
  return messages;
}

/* RFC 2812 §2.3 caps a line at 512 bytes *including* CRLF, and the cap is
** enforced here because this is the one choke point every reply and every
** relay funnels through. It cannot be enforced at the sender's edge: the
** input LineBuffer already limits what a client may send to 512, but the
** server re-frames that payload with a prefix (":nick!user@host PRIVMSG
** #chan :") before forwarding it, so a perfectly legal inbound line becomes
** an illegal outbound one. Replies that enumerate (353, 319) grow the same
** way. Truncating at the queue is what keeps all of them legal without
** auditing 100+ call sites -- callers that would rather split than lose
** bytes (RPL_NAMREPLY does) must chunk before they get here. */
void Client::queueMessage(const std::string& msg) {
  const size_t maxPayload = static_cast<size_t>(MAX_MSGLEN) - 2;
  if (msg.size() > maxPayload)
    _io.queue(msg.substr(0, maxPayload));
  else
    _io.queue(msg);
}

const std::string& Client::getSendBuffer() const { return _io.outData(); }

void Client::clearSendBuffer(size_t bytesSent) { _io.consume(bytesSent); }

bool Client::hasPendingData() const { return _io.hasPending(); }

/* True once a queued message had to be dropped because the send buffer
** blew MAX_SENDQ — the server's cue to disconnect this peer (it is either
** not reading or being flooded; its stream is already incomplete). */
bool Client::isSendQExceeded() const { return _io.overflowed(); }
