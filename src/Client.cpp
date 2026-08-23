#include "Client.hpp"

#include <ostream>
#include <string>
#include <vector>

#include "IrcTrace.hpp"
#include "Limits.hpp"
#include "Replies.hpp"
#include "Settings.hpp"

Client::Client(int fd, const std::string& hostname)
    : _fd(fd),
      _nickname("*"),
      _hostname(hostname),
      _registered(false),
      _passSent(false),
      _nickSet(false),
      _userSet(false),
      _io(Limits::kMsgLen, settings().sendQ),
      _lastActivity(std::time(NULL)),
      _pingSent(false),
      _pendingClose(false),
      _pendingCloseSince(0),
      _tearingDown(false),
      _invisible(false),
      _wallops(false) {}

Client::~Client() {}

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
bool Client::isInvisible() const { return _invisible; }
bool Client::isWallops() const { return _wallops; }
void Client::setInvisible(bool on) { _invisible = on; }
void Client::setWallops(bool on) { _wallops = on; }

std::string Client::getUserModeString() const {
  std::string modes = "+";
  if (_invisible) modes += "i";
  if (_wallops) modes += "w";
  return modes;
}

std::string Client::getPrefix() const { return _nickname + "!" + _username + "@" + _hostname; }

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

void Client::appendToRecvBuffer(const std::string& data) { _io.feed(data); }

static std::string stripNul(const std::string& line) {
  std::string out;
  out.reserve(line.size());
  for (std::string::size_type i = 0; i < line.size(); ++i) {
    if (line[i] != '\0') out += line[i];
  }
  return out;
}

static void splitOnCr(const std::string& line, std::vector<std::string>& out) {
  std::string::size_type start = 0;
  while (start <= line.size()) {
    std::string::size_type cr = line.find('\r', start);
    if (cr == std::string::npos) {
      if (start < line.size()) out.push_back(line.substr(start));
      return;
    }
    if (cr > start) out.push_back(line.substr(start, cr - start));
    start = cr + 1;
  }
}

std::vector<std::string> Client::extractMessages() {
  std::vector<std::string> messages;
  std::string line;

  while (_io.nextLine(line)) splitOnCr(stripNul(line), messages);
  return messages;
}

void Client::queueMessage(const std::string& msg) {
  const size_t maxPayload = Limits::kMsgLen - 2;
  std::string wire = (msg.size() > maxPayload) ? msg.substr(0, maxPayload) : msg;
  _io.queue(wire);

  IrcTrace::outbound(_fd, _nickname, wire);
}

const std::string& Client::getSendBuffer() const { return _io.outData(); }

void Client::clearSendBuffer(size_t bytesSent) { _io.consume(bytesSent); }

bool Client::hasPendingData() const { return _io.hasPending(); }

bool Client::isSendQExceeded() const { return _io.overflowed(); }

std::ostream& operator<<(std::ostream& os, const Client& client) {
  os << client.getPrefix() << " fd=" << client.getFd();
  os << (client.isRegistered() ? " registered" : " unregistered");
  const std::string modes = client.getUserModeString();
  if (modes != "+") os << " " << modes;
  if (client.isPendingClose()) os << " pending-close";
  if (client.isTearingDown()) os << " tearing-down";
  return os;
}
