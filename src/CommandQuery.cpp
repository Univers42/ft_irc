/* ─── Query commands: WHO, WHOIS, USERHOST ─── */

#include "Server.hpp"

/* ─── WHO ─── */

void Server::cmdWho(Client *client, const Message &msg)
{
	/* No mask (or an empty one) to enumerate, but a client that blocks until
	** the terminator arrives would hang if we answered with silence -- and
	** echoing the empty mask back would malform the reply. */
	if (msg.params.empty() || msg.params[0].empty())
	{
		sendReply(client, RPL_ENDOFWHO, "* :End of WHO list");
		return;
	}

	const std::string &target = msg.params[0];

	if (target[0] == '#')
	{
		// WHO #channel
		Channel *chan = findChannel(target);
		if (!chan)
		{
			sendReply(client, RPL_ENDOFWHO,
					  target + " :End of WHO list");
			return;
		}

		std::vector<Client *> members = chan->getMembers();
		for (size_t i = 0; i < members.size(); ++i)
		{
			Client *m = members[i];
			std::string flags = "H";
			if (chan->isOperator(m))
				flags += "@";

			// :server 352 requester channel user host server nick flags :0 realname
			sendReply(client, RPL_WHOREPLY,
					  target + " " + m->getUsername() + " "
					  + m->getHostname() + " " + _serverName + " "
					  + m->getNickname() + " " + flags
					  + " :0 " + m->getRealname());
		}
		sendReply(client, RPL_ENDOFWHO,
				  target + " :End of WHO list");
	}
	else
	{
		// WHO nickname
		Client *dest = findClientByNick(target);
		if (dest)
		{
			sendReply(client, RPL_WHOREPLY,
					  "* " + dest->getUsername() + " "
					  + dest->getHostname() + " " + _serverName + " "
					  + dest->getNickname() + " H"
					  + " :0 " + dest->getRealname());
		}
		sendReply(client, RPL_ENDOFWHO,
				  target + " :End of WHO list");
	}
}

/* ─── WHOIS ─── */

void Server::cmdWhois(Client *client, const Message &msg)
{
	/* WHOIS [<server>] <nick>: the nick is the second parameter when one is
	** given. An empty one is no nick at all. */
	if (msg.params.empty()
		|| msg.params[msg.params.size() > 1 ? 1 : 0].empty())
	{
		sendReply(client, ERR_NONICKNAMEGIVEN,
				  ":No nickname given");
		return;
	}

	const std::string &nick = msg.params[msg.params.size() > 1 ? 1 : 0];

	Client *dest = findClientByNick(nick);
	if (!dest)
	{
		sendReply(client, ERR_NOSUCHNICK,
				  nick + " :No such nick/channel");
		return;
	}

	// 311 RPL_WHOISUSER
	sendReply(client, RPL_WHOISUSER,
			  dest->getNickname() + " " + dest->getUsername() + " "
			  + dest->getHostname() + " * :" + dest->getRealname());

	// 312 RPL_WHOISSERVER
	sendReply(client, RPL_WHOISSERVER,
			  dest->getNickname() + " " + _serverName + " :ft_irc server");

	/* 319 RPL_WHOISCHANNELS — list channels the user is on.
	** ponytail: single un-chunked line, truncated by Client::queueMessage at
	** 512 bytes if the user is on enough channels (~40+). RPL_NAMREPLY got
	** real chunking because channel membership grows with normal use; this
	** one grows only with how many channels one user joined, which nothing
	** in the subject exercises. Upgrade path if it ever matters: emit one
	** 319 per chunk, the same way cmdJoin does with getNamesChunks(). */
	std::string chanList;
	for (std::map<std::string, Channel *>::const_iterator it = _channels.begin();
		 it != _channels.end(); ++it)
	{
		if (it->second->isMember(dest))
		{
			if (!chanList.empty())
				chanList += " ";
			if (it->second->isOperator(dest))
				chanList += "@";
			/* getName(), not it->first: the map key is casemapped, so
			** the key reports "#case" for a channel the client joined
			** and sees everywhere else as "#Case". */
			chanList += it->second->getName();
		}
	}
	if (!chanList.empty())
	{
		sendReply(client, RPL_WHOISCHANNELS,
				  dest->getNickname() + " :" + chanList);
	}

	// 318 RPL_ENDOFWHOIS
	sendReply(client, RPL_ENDOFWHOIS,
			  dest->getNickname() + " :End of WHOIS list");
}

/* ─── USERHOST ─── */

void Server::cmdUserhost(Client *client, const Message &msg)
{
	if (msg.params.empty())
	{
		sendReply(client, ERR_NEEDMOREPARAMS,
				  "USERHOST :Not enough parameters");
		return;
	}

	std::string reply;
	for (size_t i = 0; i < msg.params.size() && i < 5; ++i)
	{
		Client *dest = findClientByNick(msg.params[i]);
		if (dest)
		{
			if (!reply.empty())
				reply += " ";
			reply += dest->getNickname() + "=+"
					 + dest->getUsername() + "@" + dest->getHostname();
		}
	}

	sendReply(client, RPL_USERHOST, ":" + reply);
}
