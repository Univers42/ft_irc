#include "bonus/FileTransferExt.hpp"

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <string>
#include <vector>

#include "Client.hpp"
#include "IrcCase.hpp"
#include "IrcMessage.hpp"
#include "Limits.hpp"
#include "Server.hpp"
#include "Settings.hpp"
#include "libcpp/str/base64.hpp"
#include "libcpp/str/case.hpp"
#include "libcpp/str/format.hpp"
#include "libcpp/str/secure.hpp"

namespace {
/* Each of these is written at more than one site. The two abort reasons are
** relayed to the peer, so a divergence between copies would be visible. */
const char* const kAbortPeerGone = "peer disconnected";
const char* const kAbortBadChunk = "malformed chunk";
const char* const kNoSuchTransfer = "FILE: no transfer with id ";
}  // namespace

const FileTransferExt::SubCommand FileTransferExt::kSubCommands[] = {
    {"SEND", &FileTransferExt::cmdSend},
    {"ACCEPT", &FileTransferExt::cmdAccept},
    {"REJECT", &FileTransferExt::cmdReject},
    {"DATA", &FileTransferExt::cmdData},
    {"END", &FileTransferExt::cmdEnd},
    {"ABORT", &FileTransferExt::cmdAbort},
    {NULL, NULL},
};

FileTransferExt::FileTransferExt() : _transfers() {}

void FileTransferExt::cmdAccept(Server& server, Client& client, const Message& msg) {
  cmdAnswer(server, client, msg, true);
}

void FileTransferExt::cmdReject(Server& server, Client& client, const Message& msg) {
  cmdAnswer(server, client, msg, false);
}

FileTransferExt::~FileTransferExt() {}

const char* FileTransferExt::name() const { return "file-transfer"; }

/* is_safe_path_component covers the filesystem half — empty, "." and "..",
** separators, control bytes, DEL. Space and comma are ours to add: IRC's
** wire format is space-delimited with comma-separated lists, so a filename
** carrying either would reframe the command it travels in. */
static bool isValidFilename(const std::string& name) {
  return name.size() <= FileTransferExt::MAX_FILENAME && libcpp::str::is_safe_path_component(name, " ,");
}

static bool parseId(const std::string& s, long& id) { return libcpp::str::parse_long(s, 1, LONG_MAX, id); }

void FileTransferExt::notice(Server& server, Client& client, const std::string& text) {
  server.sendToClient(&client, "NOTICE " + client.getNickname() + " :" + text);
}

FileTransferExt::Transfer* FileTransferExt::findById(long id) { return _transfers.find(id); }

long FileTransferExt::findActive(int senderFd, int recipientFd) const {
  for (Registry::const_iterator it = _transfers.begin(); it != _transfers.end(); ++it) {
    const Transfer& t = it->second.value;
    if (t.senderFd == senderFd && t.recipientFd == recipientFd) return it->first;
  }
  return 0;  //< id 0 is never allocated, so it is free to mean "none active"
}

void FileTransferExt::abortTransfer(Server& server, long id, const std::string& why) {
  Transfer* t = _transfers.find(id);
  if (t == NULL) return;

  std::string line = "FILE ABRT " + libcpp::str::to_string(id) + " :" + why;
  Client* sender = server.findClientByFd(t->senderFd);
  Client* recipient = server.findClientByFd(t->recipientFd);
  if (sender) notice(server, *sender, line);
  if (recipient) notice(server, *recipient, line);
  _transfers.erase(id);
}

bool FileTransferExt::onCommand(Server& server, Client& client, const Message& msg) {
  if (msg.command != "FILE") return false;

  if (msg.params.empty()) {
    notice(server, client,
           "FILE usage: SEND <nick> <file> <size> | ACCEPT/REJECT <id> | "
           "DATA <id> <b64> | END <id> | ABORT <id>");
    return true;
  }

  const std::string sub = libcpp::str::to_upper(msg.params[0]);
  const SubCommand* entry = Dispatch::find(kSubCommands, sub);
  if (entry == NULL) {
    notice(server, client, "FILE: unknown subcommand " + sub);
    return true;
  }

  (this->*entry->handler)(server, client, msg);
  return true;
}

void FileTransferExt::onClientDisconnect(Server& server, Client& client, const std::string& reason) {
  (void)reason;
  const int fd = client.getFd();
  std::vector<Registry::Id> doomed;
  for (Registry::const_iterator it = _transfers.begin(); it != _transfers.end(); ++it) {
    const Transfer& t = it->second.value;
    if (t.senderFd == fd || t.recipientFd == fd) doomed.push_back(it->first);
  }
  //< abortTransfer erases, so the scan has to finish before any of it runs
  for (std::size_t i = 0; i < doomed.size(); ++i) abortTransfer(server, doomed[i], kAbortPeerGone);
}

void FileTransferExt::onTick(Server& server, time_t now) {
  std::vector<Registry::Id> expired;
  _transfers.collectExpired(now, IDLE_TIMEOUT, expired);
  for (std::size_t i = 0; i < expired.size(); ++i) abortTransfer(server, expired[i], "timeout");
}

void FileTransferExt::cmdSend(Server& server, Client& client, const Message& msg) {
  if (msg.params.size() < 4) {
    notice(server, client, "FILE SEND usage: FILE SEND <nick> <file> <size>");
    return;
  }
  const std::string& nick = msg.params[1];
  const std::string& filename = msg.params[2];

  Client* recipient = server.findClientByNick(nick);
  if (!recipient || !recipient->isRegistered()) {
    notice(server, client, "FILE: no such nick " + nick);
    return;
  }
  if (recipient == &client) {
    notice(server, client, "FILE: cannot send to yourself");
    return;
  }
  if (!isValidFilename(filename)) {
    notice(server, client, "FILE: invalid filename");
    return;
  }

  unsigned long size = 0;
  if (!libcpp::str::parse_ulong(msg.params[3], 1, MAX_FILE_SIZE, size)) {
    notice(server, client, "FILE: invalid size (1.." + libcpp::str::to_string(MAX_FILE_SIZE) + ")");
    return;
  }

  if (findActive(client.getFd(), recipient->getFd()) != 0) {
    notice(server, client, "FILE: a transfer to " + nick + " is already active");
    return;
  }

  Transfer t;
  t.senderFd = client.getFd();
  t.recipientFd = recipient->getFd();
  t.filename = filename;
  t.declaredSize = size;
  t.relayedBytes = 0;
  t.accepted = false;
  const long id = _transfers.add(t, std::time(NULL));

  recipient->queueMessage(
      IrcMessage::relay(client.getPrefix(), "FILE",
                        "OFFER " + libcpp::str::to_string(id) + " " + filename + " " + libcpp::str::to_string(size)));
  notice(server, client, "FILE " + libcpp::str::to_string(id) + " offered to " + nick);
}

void FileTransferExt::cmdAnswer(Server& server, Client& client, const Message& msg, bool accept) {
  long id = 0;
  if (msg.params.size() < 2 || !parseId(msg.params[1], id)) {
    notice(server, client, "FILE: usage: FILE ACCEPT|REJECT <id>");
    return;
  }
  Transfer* t = findById(id);
  if (!t || t->recipientFd != client.getFd()) {
    notice(server, client, "FILE: no offer with id " + msg.params[1]);
    return;
  }

  Client* sender = server.findClientByFd(t->senderFd);
  if (!sender) {
    _transfers.erase(id);
    notice(server, client, "FILE: sender is gone");
    return;
  }

  _transfers.touch(id, std::time(NULL));
  if (accept) {
    t->accepted = true;
    sender->queueMessage(IrcMessage::relay(client.getPrefix(), "FILE", "OK " + libcpp::str::to_string(id)));
  } else {
    sender->queueMessage(IrcMessage::relay(client.getPrefix(), "FILE", "NO " + libcpp::str::to_string(id)));
    _transfers.erase(id);
  }
}

void FileTransferExt::cmdData(Server& server, Client& client, const Message& msg) {
  long id = 0;
  if (msg.params.size() < 3 || !parseId(msg.params[1], id)) {
    notice(server, client, "FILE: usage: FILE DATA <id> <base64>");
    return;
  }
  Transfer* t = findById(id);
  if (!t || t->senderFd != client.getFd()) {
    notice(server, client, kNoSuchTransfer + msg.params[1]);
    return;
  }
  if (!t->accepted) {
    notice(server, client, "FILE: transfer " + msg.params[1] + " not accepted yet");
    return;
  }

  const std::string& chunk = msg.params[2];
  //< is_base64 is strict (length%4, padding only at the end), so a chunk that
  //< would decode to garbage is refused here rather than relayed onward
  if (chunk.size() > MAX_CHUNK_B64 || !libcpp::str::is_base64(chunk)) {
    abortTransfer(server, id, kAbortBadChunk);
    return;
  }

  const unsigned long decoded = libcpp::str::base64_decoded_size(chunk);
  if (decoded == 0) {  //< also catches the empty chunk
    abortTransfer(server, id, kAbortBadChunk);
    return;
  }
  if (t->relayedBytes + decoded > t->declaredSize) {
    abortTransfer(server, id, "size overrun");
    return;
  }

  Client* recipient = server.findClientByFd(t->recipientFd);
  if (!recipient) {
    abortTransfer(server, id, kAbortPeerGone);
    return;
  }

  if (recipient->getSendBuffer().size() > settings().sendQ / 2) {
    notice(server, client, "FILE WAIT " + msg.params[1]);
    return;
  }

  t->relayedBytes += decoded;
  _transfers.touch(id, std::time(NULL));
  recipient->queueMessage(IrcMessage::relay(client.getPrefix(), "FILE", "DATA " + msg.params[1] + " " + chunk));
}

void FileTransferExt::cmdEnd(Server& server, Client& client, const Message& msg) {
  long id = 0;
  if (msg.params.size() < 2 || !parseId(msg.params[1], id)) {
    notice(server, client, "FILE: usage: FILE END <id>");
    return;
  }
  Transfer* t = findById(id);
  if (!t || t->senderFd != client.getFd()) {
    notice(server, client, kNoSuchTransfer + msg.params[1]);
    return;
  }

  Client* recipient = server.findClientByFd(t->recipientFd);
  if (recipient)
    recipient->queueMessage(IrcMessage::relay(client.getPrefix(), "FILE",
                                              "END " + msg.params[1] + " " + libcpp::str::to_string(t->relayedBytes)));
  _transfers.erase(id);
}

void FileTransferExt::cmdAbort(Server& server, Client& client, const Message& msg) {
  long id = 0;
  if (msg.params.size() < 2 || !parseId(msg.params[1], id)) {
    notice(server, client, "FILE: usage: FILE ABORT <id>");
    return;
  }
  Transfer* t = findById(id);
  if (!t || (t->senderFd != client.getFd() && t->recipientFd != client.getFd())) {
    notice(server, client, kNoSuchTransfer + msg.params[1]);
    return;
  }
  abortTransfer(server, id, "aborted by " + client.getNickname());
}
