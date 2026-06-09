#ifndef FILETRANSFEREXT_HPP
# define FILETRANSFEREXT_HPP

# include <map>
# include <string>

# include "ext/IServerExtension.hpp"

/*
** FileTransferExt — bonus: server-mediated file transfer.
**
** Relay-only: the server never decodes base64 nor touches the disk; it
** validates and forwards chunks between two registered clients. The payload
** is base64, so every relayed line is printable and stays under the 512-byte
** IRC line limit by construction.
**
** Client protocol (FILE is consumed via the extension seam's onCommand —
** it can never shadow a core RFC command):
**
**   sender:     FILE SEND <nick> <filename> <size>
**     -> recipient:  :<sender>  FILE OFFER <id> <filename> <size>
**   recipient:  FILE ACCEPT <id>   |   FILE REJECT <id>
**     -> sender:     :<recipient> FILE OK <id>  |  FILE NO <id>
**   sender:     FILE DATA <id> <base64-chunk ≤ 400 chars>
**     -> recipient:  :<sender>  FILE DATA <id> <chunk>
**   sender:     FILE END <id>
**   either:     FILE ABORT <id>
**
** Flow control: when the recipient's send queue is already half full the
** chunk is NOT relayed — the sender receives "FILE WAIT <id>" and retries.
** Transfers idle for 60 s are aborted (onTick); a disconnect aborts every
** transfer involving that client and notifies the peer.
*/
class FileTransferExt : public IServerExtension
{
public:
	FileTransferExt();
	~FileTransferExt();

	/* ─── IServerExtension ─── */
	const char	*name() const;
	bool		onCommand(Server &server, Client &client, const Message &msg);
	void		onClientDisconnect(Server &server, Client &client,
								   const std::string &reason);
	void		onTick(Server &server, time_t now);

	/* protocol limits */
	static const unsigned long	MAX_FILE_SIZE = 50UL * 1024UL * 1024UL;
	static const size_t			MAX_CHUNK_B64 = 400;
	static const size_t			MAX_FILENAME = 64;
	static const time_t			IDLE_TIMEOUT = 60;

private:
	FileTransferExt(const FileTransferExt &);
	FileTransferExt &operator=(const FileTransferExt &);

	struct Transfer
	{
		int				senderFd;
		int				recipientFd;
		std::string		filename;
		unsigned long	declaredSize;
		unsigned long	relayedBytes;
		bool			accepted;
		time_t			lastActivity;
	};

	void	cmdSend(Server &server, Client &client, const Message &msg);
	void	cmdAnswer(Server &server, Client &client, const Message &msg,
					  bool accept);
	void	cmdData(Server &server, Client &client, const Message &msg);
	void	cmdEnd(Server &server, Client &client, const Message &msg);
	void	cmdAbort(Server &server, Client &client, const Message &msg);

	/* helpers */
	void	notice(Server &server, Client &client, const std::string &text);
	void	abortTransfer(Server &server, long id, const std::string &why);
	Transfer	*findById(long id);
	long	findActive(int senderFd, int recipientFd) const;

	std::map<long, Transfer>	_transfers;
	long						_nextId;
};

#endif /* FILETRANSFEREXT_HPP */
