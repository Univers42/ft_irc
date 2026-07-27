/* ─── Channel commands: JOIN, PART ─── */

#include "Server.hpp"
#include "ext/IServerExtension.hpp"
#include "libcpp/str/format.hpp"

/* ─── JOIN ─── */

void Server::cmdJoin(Client *client, const Message &msg)
{
	/* A trailing ":" parses to a present-but-empty parameter. An empty
	** channel name is a missing one -- answer it, rather than falling
	** through to a loop that iterates zero times and replies nothing. */
	if (msg.params.empty() || msg.params[0].empty())
	{
		sendReply(client, ERR_NEEDMOREPARAMS,
				  "JOIN :Not enough parameters");
		return;
	}

	/* Channel names: blanks dropped ("#a,,#b" is two channels). Keys:
	** blanks KEPT, because they are positional -- "k1,,k3" means the second
	** channel has no key, not that there are two keys. */
	std::vector<std::string> channels =
		libcpp::str::split_nonempty(msg.params[0], ',');

	std::vector<std::string> keys;
	if (msg.params.size() > 1)
		keys = libcpp::str::split(msg.params[1], ',');

	for (size_t i = 0; i < channels.size(); ++i)
	{
		const std::string &name = channels[i];
		std::string key = (i < keys.size()) ? keys[i] : "";

		if (!isValidChannelName(name))
		{
			sendReply(client, ERR_BADCHANMASK,
					  name + " :Bad Channel Mask");
			continue;
		}

		Channel *chan = findChannel(name);
		if (chan)
		{
			// Channel exists — check restrictions
			if (chan->isMember(client))
				continue; // Already in channel

			// Check invite-only
			if (chan->isInviteOnly() && !chan->isInvited(client))
			{
				sendReply(client, ERR_INVITEONLYCHAN,
						  name + " :Cannot join channel (+i)");
				continue;
			}

			// Check channel key
			if (!chan->getKey().empty() && chan->getKey() != key)
			{
				sendReply(client, ERR_BADCHANNELKEY,
						  name + " :Cannot join channel (+k)");
				continue;
			}

			// Check user limit
			if (chan->getUserLimit() > 0
				&& chan->getMemberCount() >= chan->getUserLimit())
			{
				sendReply(client, ERR_CHANNELISFULL,
						  name + " :Cannot join channel (+l)");
				continue;
			}

			chan->addMember(client);
			chan->removeInvite(client);
		}
		else
		{
			// Create new channel — creator becomes operator
			chan = createChannel(name, client);
			if (!chan)
			{
				sendReply(client, ERR_NOSUCHCHANNEL,
						  name + " :Cannot create channel (server error)");
				continue;
			}
		}

		/* Broadcast JOIN to all channel members (including the joiner),
		** naming the channel in its canonical stored case rather than
		** however this joiner spelled it. */
		chan->broadcastMessage(":" + client->getPrefix()
							  + " JOIN " + chan->getName(), NULL);
		audit("join", client->getNickname(), name);
		for (size_t k = 0; k < _extensions.size(); ++k)
			_extensions[k]->onJoin(*this, *client, *chan);

		// Send topic
		if (!chan->getTopic().empty())
		{
			sendReply(client, RPL_TOPIC,
					  name + " :" + chan->getTopic());
			sendReply(client, RPL_TOPICWHOTIME,
					  name + " " + chan->getTopicSetter() + " "
					  + libcpp::str::to_string(chan->getTopicTime()));
		}
		else
		{
			sendReply(client, RPL_NOTOPIC,
					  name + " :No topic is set");
		}

		/* Send the names list, split across as many 353s as the 512-byte
		** line limit needs. The budget is what's left of a legal line once
		** sendReply's own framing (":<server> 353 <nick> ") and the
		** "= <channel> :" head are accounted for. */
		std::string namesHead = "= " + name + " :";
		size_t framing = 1 + _serverName.size() + 1 + 3 + 1
						 + client->getNickname().size() + 1
						 + namesHead.size();
		size_t budget = (framing + 1 < static_cast<size_t>(MAX_MSGLEN) - 2)
						? static_cast<size_t>(MAX_MSGLEN) - 2 - framing : 1;
		std::vector<std::string> chunks = chan->getNamesChunks(budget);
		for (size_t c = 0; c < chunks.size(); ++c)
			sendReply(client, RPL_NAMREPLY, namesHead + chunks[c]);
		sendReply(client, RPL_ENDOFNAMES,
				  name + " :End of /NAMES list");

		// Proactively report channel modes (mirrors the MODE query) so the
		// client sees +itkl state on join without having to ask.
		std::string modes = chan->getModeString();
		std::string modeParams = chan->getModeParams();
		std::string modeReply = name + " " + modes;
		if (!modeParams.empty())
			modeReply += " " + modeParams;
		sendReply(client, RPL_CHANNELMODEIS, modeReply);
		sendReply(client, RPL_CREATIONTIME,
				  name + " " + libcpp::str::to_string(chan->getCreationTime()));
	}
}

/* ─── PART ─── */

void Server::cmdPart(Client *client, const Message &msg)
{
	if (msg.params.empty() || msg.params[0].empty())
	{
		sendReply(client, ERR_NEEDMOREPARAMS,
				  "PART :Not enough parameters");
		return;
	}

	std::string reason;
	if (msg.params.size() > 1)
		reason = msg.params[1];

	std::vector<std::string> targets =
		libcpp::str::split_nonempty(msg.params[0], ',');

	for (size_t t = 0; t < targets.size(); ++t)
	{
		const std::string &chanName = targets[t];
		Channel *chan = findChannel(chanName);
		if (!chan)
		{
			sendReply(client, ERR_NOSUCHCHANNEL,
					  chanName + " :No such channel");
			continue;
		}

		if (!chan->isMember(client))
		{
			sendReply(client, ERR_NOTONCHANNEL,
					  chanName + " :You're not on that channel");
			continue;
		}

		std::string partMsg = ":" + client->getPrefix()
							  + " PART " + chan->getName();
		if (!reason.empty())
			partMsg += " :" + reason;

		chan->broadcastMessage(partMsg, NULL);
		audit("part", client->getNickname(), chanName);
		for (size_t k = 0; k < _extensions.size(); ++k)
			_extensions[k]->onPart(*this, *client, *chan);
		chan->removeMember(client);

		if (chan->isEmpty())
			removeChannel(chanName);
	}
}
