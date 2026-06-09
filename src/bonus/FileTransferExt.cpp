/* ─── Bonus: server-mediated file transfer (see header for the protocol) ─── */

#include "bonus/FileTransferExt.hpp"

#include "Server.hpp"
#include "Client.hpp"
#include "IrcCase.hpp"
#include "libcpp/str/case.hpp"
#include "libcpp/str/format.hpp"

#include <cerrno>
#include <cstdlib>

FileTransferExt::FileTransferExt()
	: _transfers(),
	  _nextId(1)
{
}

FileTransferExt::~FileTransferExt() {}

const char *FileTransferExt::name() const
{
	return "file-transfer";
}

/* ─── validation helpers ─── */

static bool isValidFilename(const std::string &name)
{
	if (name.empty() || name.size() > FileTransferExt::MAX_FILENAME)
		return false;
	if (name == "." || name == "..")
		return false;
	for (std::string::size_type i = 0; i < name.size(); ++i)
	{
		unsigned char c = static_cast<unsigned char>(name[i]);
		if (c <= ' ' || c == 0x7F || c == '/' || c == '\\' || c == ',')
			return false;
	}
	return true;
}

static bool isBase64Chunk(const std::string &chunk)
{
	if (chunk.empty() || chunk.size() > FileTransferExt::MAX_CHUNK_B64)
		return false;
	for (std::string::size_type i = 0; i < chunk.size(); ++i)
	{
		char c = chunk[i];
		bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
			   || (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=';
		if (!ok)
			return false;
	}
	return true;
}

/* Decoded byte count of one base64 chunk (upper bound, padding-aware). */
static unsigned long decodedBytes(const std::string &chunk)
{
	unsigned long pad = 0;
	if (!chunk.empty() && chunk[chunk.size() - 1] == '=')
		++pad;
	if (chunk.size() > 1 && chunk[chunk.size() - 2] == '=')
		++pad;
	return (static_cast<unsigned long>(chunk.size()) * 3UL) / 4UL - pad;
}

static bool parseId(const std::string &s, long &id)
{
	errno = 0;
	char *end = NULL;
	long v = std::strtol(s.c_str(), &end, 10);
	if (errno == ERANGE || end == s.c_str() || *end != '\0' || v <= 0)
		return false;
	id = v;
	return true;
}

/* ─── plumbing ─── */

void FileTransferExt::notice(Server &server, Client &client,
							 const std::string &text)
{
	server.sendToClient(&client, "NOTICE " + client.getNickname()
						+ " :" + text);
}

FileTransferExt::Transfer *FileTransferExt::findById(long id)
{
	std::map<long, Transfer>::iterator it = _transfers.find(id);
	return it == _transfers.end() ? NULL : &it->second;
}

long FileTransferExt::findActive(int senderFd, int recipientFd) const
{
	for (std::map<long, Transfer>::const_iterator it = _transfers.begin();
		 it != _transfers.end(); ++it)
	{
		if (it->second.senderFd == senderFd
			&& it->second.recipientFd == recipientFd)
			return it->first;
	}
	return 0;
}

void FileTransferExt::abortTransfer(Server &server, long id,
									const std::string &why)
{
	std::map<long, Transfer>::iterator it = _transfers.find(id);
	if (it == _transfers.end())
		return;

	std::string line = "FILE ABRT " + libcpp::str::to_string(id) + " :" + why;
	Client *sender = server.findClientByFd(it->second.senderFd);
	Client *recipient = server.findClientByFd(it->second.recipientFd);
	if (sender)
		notice(server, *sender, line);
	if (recipient)
		notice(server, *recipient, line);
	_transfers.erase(it);
}

/* ─── IServerExtension ─── */

bool FileTransferExt::onCommand(Server &server, Client &client,
								const Message &msg)
{
	if (msg.command != "FILE")
		return false;

	if (msg.params.empty())
	{
		notice(server, client,
			   "FILE usage: SEND <nick> <file> <size> | ACCEPT/REJECT <id> | "
			   "DATA <id> <b64> | END <id> | ABORT <id>");
		return true;
	}

	std::string sub = libcpp::str::to_upper(msg.params[0]);
	if (sub == "SEND")
		cmdSend(server, client, msg);
	else if (sub == "ACCEPT")
		cmdAnswer(server, client, msg, true);
	else if (sub == "REJECT")
		cmdAnswer(server, client, msg, false);
	else if (sub == "DATA")
		cmdData(server, client, msg);
	else if (sub == "END")
		cmdEnd(server, client, msg);
	else if (sub == "ABORT")
		cmdAbort(server, client, msg);
	else
		notice(server, client, "FILE: unknown subcommand " + sub);
	return true;
}

void FileTransferExt::onClientDisconnect(Server &server, Client &client,
										 const std::string &reason)
{
	(void)reason;
	int fd = client.getFd();
	std::map<long, Transfer>::iterator it = _transfers.begin();
	while (it != _transfers.end())
	{
		if (it->second.senderFd == fd || it->second.recipientFd == fd)
		{
			long id = it->first;
			++it;
			/* the leaver's copy of the notice is discarded with its queue */
			abortTransfer(server, id, "peer disconnected");
		}
		else
			++it;
	}
}

void FileTransferExt::onTick(Server &server, time_t now)
{
	std::map<long, Transfer>::iterator it = _transfers.begin();
	while (it != _transfers.end())
	{
		if (now - it->second.lastActivity > IDLE_TIMEOUT)
		{
			long id = it->first;
			++it;
			abortTransfer(server, id, "timeout");
		}
		else
			++it;
	}
}

/* ─── subcommands ─── */

void FileTransferExt::cmdSend(Server &server, Client &client,
							  const Message &msg)
{
	if (msg.params.size() < 4)
	{
		notice(server, client, "FILE SEND usage: FILE SEND <nick> <file> <size>");
		return;
	}
	const std::string &nick = msg.params[1];
	const std::string &filename = msg.params[2];

	Client *recipient = server.findClientByNick(nick);
	if (!recipient || !recipient->isRegistered())
	{
		notice(server, client, "FILE: no such nick " + nick);
		return;
	}
	if (recipient == &client)
	{
		notice(server, client, "FILE: cannot send to yourself");
		return;
	}
	if (!isValidFilename(filename))
	{
		notice(server, client, "FILE: invalid filename");
		return;
	}

	errno = 0;
	char *end = NULL;
	unsigned long size = std::strtoul(msg.params[3].c_str(), &end, 10);
	if (errno == ERANGE || end == msg.params[3].c_str() || *end != '\0'
		|| size == 0 || size > MAX_FILE_SIZE)
	{
		notice(server, client, "FILE: invalid size (1.."
			   + libcpp::str::to_string(MAX_FILE_SIZE) + ")");
		return;
	}

	if (findActive(client.getFd(), recipient->getFd()) != 0)
	{
		notice(server, client, "FILE: a transfer to " + nick
			   + " is already active");
		return;
	}

	long id = _nextId++;
	Transfer t;
	t.senderFd = client.getFd();
	t.recipientFd = recipient->getFd();
	t.filename = filename;
	t.declaredSize = size;
	t.relayedBytes = 0;
	t.accepted = false;
	t.lastActivity = std::time(NULL);
	_transfers[id] = t;

	recipient->queueMessage(":" + client.getPrefix() + " FILE OFFER "
							+ libcpp::str::to_string(id) + " " + filename
							+ " " + libcpp::str::to_string(size));
	notice(server, client, "FILE " + libcpp::str::to_string(id)
		   + " offered to " + nick);
	server.audit("file-offer", client.getNickname(),
				 nick + " " + filename + " "
				 + libcpp::str::to_string(size));
}

void FileTransferExt::cmdAnswer(Server &server, Client &client,
								const Message &msg, bool accept)
{
	long id = 0;
	if (msg.params.size() < 2 || !parseId(msg.params[1], id))
	{
		notice(server, client, "FILE: usage: FILE ACCEPT|REJECT <id>");
		return;
	}
	Transfer *t = findById(id);
	if (!t || t->recipientFd != client.getFd())
	{
		notice(server, client, "FILE: no offer with id " + msg.params[1]);
		return;
	}

	Client *sender = server.findClientByFd(t->senderFd);
	if (!sender)
	{
		_transfers.erase(id);
		notice(server, client, "FILE: sender is gone");
		return;
	}

	t->lastActivity = std::time(NULL);
	if (accept)
	{
		t->accepted = true;
		sender->queueMessage(":" + client.getPrefix() + " FILE OK "
							 + libcpp::str::to_string(id));
		server.audit("file-accept", client.getNickname(), t->filename);
	}
	else
	{
		sender->queueMessage(":" + client.getPrefix() + " FILE NO "
							 + libcpp::str::to_string(id));
		server.audit("file-reject", client.getNickname(), t->filename);
		_transfers.erase(id);
	}
}

void FileTransferExt::cmdData(Server &server, Client &client,
							  const Message &msg)
{
	long id = 0;
	if (msg.params.size() < 3 || !parseId(msg.params[1], id))
	{
		notice(server, client, "FILE: usage: FILE DATA <id> <base64>");
		return;
	}
	Transfer *t = findById(id);
	if (!t || t->senderFd != client.getFd())
	{
		notice(server, client, "FILE: no transfer with id " + msg.params[1]);
		return;
	}
	if (!t->accepted)
	{
		notice(server, client, "FILE: transfer " + msg.params[1]
			   + " not accepted yet");
		return;
	}

	const std::string &chunk = msg.params[2];
	if (!isBase64Chunk(chunk))
	{
		abortTransfer(server, id, "malformed chunk");
		return;
	}
	if (t->relayedBytes + decodedBytes(chunk) > t->declaredSize)
	{
		abortTransfer(server, id, "size overrun");
		return;
	}

	Client *recipient = server.findClientByFd(t->recipientFd);
	if (!recipient)
	{
		abortTransfer(server, id, "peer disconnected");
		return;
	}

	/* Flow control: never let a fast sender blow the recipient's SENDQ —
	** ask the sender to retry once the recipient has drained. */
	if (recipient->getSendBuffer().size() > MAX_SENDQ / 2)
	{
		notice(server, client, "FILE WAIT " + msg.params[1]);
		return;
	}

	t->relayedBytes += decodedBytes(chunk);
	t->lastActivity = std::time(NULL);
	recipient->queueMessage(":" + client.getPrefix() + " FILE DATA "
							+ msg.params[1] + " " + chunk);
}

void FileTransferExt::cmdEnd(Server &server, Client &client,
							 const Message &msg)
{
	long id = 0;
	if (msg.params.size() < 2 || !parseId(msg.params[1], id))
	{
		notice(server, client, "FILE: usage: FILE END <id>");
		return;
	}
	Transfer *t = findById(id);
	if (!t || t->senderFd != client.getFd())
	{
		notice(server, client, "FILE: no transfer with id " + msg.params[1]);
		return;
	}

	Client *recipient = server.findClientByFd(t->recipientFd);
	if (recipient)
		recipient->queueMessage(":" + client.getPrefix() + " FILE END "
								+ msg.params[1] + " "
								+ libcpp::str::to_string(t->relayedBytes));
	server.audit("file-end", client.getNickname(),
				 t->filename + " " + libcpp::str::to_string(t->relayedBytes));
	_transfers.erase(id);
}

void FileTransferExt::cmdAbort(Server &server, Client &client,
							   const Message &msg)
{
	long id = 0;
	if (msg.params.size() < 2 || !parseId(msg.params[1], id))
	{
		notice(server, client, "FILE: usage: FILE ABORT <id>");
		return;
	}
	Transfer *t = findById(id);
	if (!t || (t->senderFd != client.getFd()
			   && t->recipientFd != client.getFd()))
	{
		notice(server, client, "FILE: no transfer with id " + msg.params[1]);
		return;
	}
	abortTransfer(server, id, "aborted by " + client.getNickname());
}
