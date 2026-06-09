/* ─── Operator commands: KICK, INVITE, TOPIC, MODE ─── */

#include "Server.hpp"
#include "libcpp/str/format.hpp"

#include <cerrno>
#include <cstdlib>
#include <iostream>

/* A channel key must be short and contain no space, comma or control
** character — it travels as a single middle parameter of JOIN/MODE. */
static bool isValidChannelKey(const std::string &key)
{
	if (key.empty() || key.size() > MAX_KEYLEN)
		return false;
	for (std::string::size_type i = 0; i < key.size(); ++i)
	{
		unsigned char c = static_cast<unsigned char>(key[i]);
		if (c <= ' ' || c == ',')
			return false;
	}
	return true;
}

/* ─── KICK ─── */

void Server::cmdKick(Client *client, const Message &msg)
{
	if (msg.params.size() < 2)
	{
		sendReply(client, ERR_NEEDMOREPARAMS,
				  "KICK :Not enough parameters");
		return;
	}

	const std::string &chanName = msg.params[0];
	const std::string &target = msg.params[1];
	std::string reason = client->getNickname();
	if (msg.params.size() > 2)
		reason = msg.params[2];

	Channel *chan = findChannel(chanName);
	if (!chan)
	{
		sendReply(client, ERR_NOSUCHCHANNEL,
				  chanName + " :No such channel");
		return;
	}

	if (!chan->isMember(client))
	{
		sendReply(client, ERR_NOTONCHANNEL,
				  chanName + " :You're not on that channel");
		return;
	}

	if (!chan->isOperator(client))
	{
		sendReply(client, ERR_CHANOPRIVSNEEDED,
				  chanName + " :You're not channel operator");
		return;
	}

	Client *targetClient = chan->findMember(target);
	if (!targetClient)
	{
		sendReply(client, ERR_USERNOTINCHANNEL,
				  target + " " + chanName + " :They aren't on that channel");
		return;
	}

	std::string kickMsg = ":" + client->getPrefix() + " KICK "
						  + chanName + " " + target + " :" + reason;
	chan->broadcastMessage(kickMsg, NULL);
	chan->removeMember(targetClient);

	if (chan->isEmpty())
		removeChannel(chanName);
}

/* ─── INVITE ─── */

void Server::cmdInvite(Client *client, const Message &msg)
{
	if (msg.params.size() < 2)
	{
		sendReply(client, ERR_NEEDMOREPARAMS,
				  "INVITE :Not enough parameters");
		return;
	}

	const std::string &target = msg.params[0];
	const std::string &chanName = msg.params[1];

	Channel *chan = findChannel(chanName);
	if (!chan)
	{
		sendReply(client, ERR_NOSUCHCHANNEL,
				  chanName + " :No such channel");
		return;
	}

	if (!chan->isMember(client))
	{
		sendReply(client, ERR_NOTONCHANNEL,
				  chanName + " :You're not on that channel");
		return;
	}

	if (chan->isInviteOnly() && !chan->isOperator(client))
	{
		sendReply(client, ERR_CHANOPRIVSNEEDED,
				  chanName + " :You're not channel operator");
		return;
	}

	Client *targetClient = findClientByNick(target);
	if (!targetClient)
	{
		sendReply(client, ERR_NOSUCHNICK,
				  target + " :No such nick/channel");
		return;
	}

	if (chan->isMember(targetClient))
	{
		sendReply(client, ERR_USERONCHANNEL,
				  target + " " + chanName + " :is already on channel");
		return;
	}

	// Add to invite list (so they can bypass +i)
	chan->addInvite(target);

	// Confirm to sender
	sendReply(client, RPL_INVITING, target + " " + chanName);

	// Notify target
	targetClient->queueMessage(":" + client->getPrefix()
							   + " INVITE " + target + " :" + chanName);
}

/* ─── TOPIC ─── */

void Server::cmdTopic(Client *client, const Message &msg)
{
	if (msg.params.empty())
	{
		sendReply(client, ERR_NEEDMOREPARAMS,
				  "TOPIC :Not enough parameters");
		return;
	}

	const std::string &chanName = msg.params[0];
	Channel *chan = findChannel(chanName);

	if (!chan)
	{
		sendReply(client, ERR_NOSUCHCHANNEL,
				  chanName + " :No such channel");
		return;
	}

	if (!chan->isMember(client))
	{
		sendReply(client, ERR_NOTONCHANNEL,
				  chanName + " :You're not on that channel");
		return;
	}

	if (msg.params.size() == 1)
	{
		// Query topic
		if (chan->getTopic().empty())
		{
			sendReply(client, RPL_NOTOPIC,
					  chanName + " :No topic is set");
		}
		else
		{
			sendReply(client, RPL_TOPIC,
					  chanName + " :" + chan->getTopic());
			sendReply(client, RPL_TOPICWHOTIME,
					  chanName + " " + chan->getTopicSetter()
					  + " " + libcpp::str::to_string(chan->getTopicTime()));
		}
		return;
	}

	// Set topic
	if (chan->isTopicRestricted() && !chan->isOperator(client))
	{
		sendReply(client, ERR_CHANOPRIVSNEEDED,
				  chanName + " :You're not channel operator");
		return;
	}

	/* Enforce the TOPICLEN=390 advertised in 005: truncate, don't reject. */
	std::string newTopic = msg.params[1];
	if (newTopic.size() > MAX_TOPICLEN)
		newTopic.erase(MAX_TOPICLEN);
	chan->setTopic(newTopic, client->getNickname());

	chan->broadcastMessage(":" + client->getPrefix() + " TOPIC "
						  + chanName + " :" + newTopic, NULL);
}

/* ─── MODE ─── */

void Server::cmdMode(Client *client, const Message &msg)
{
	if (msg.params.empty())
	{
		sendReply(client, ERR_NEEDMOREPARAMS,
				  "MODE :Not enough parameters");
		return;
	}

	const std::string &target = msg.params[0];

	if (target[0] == '#')
	{
		// Channel mode
		Channel *chan = findChannel(target);
		if (!chan)
		{
			sendReply(client, ERR_NOSUCHCHANNEL,
					  target + " :No such channel");
			return;
		}
		if (msg.params.size() == 1)
		{
			// Mode query
			std::string modes = chan->getModeString();
			std::string params = chan->getModeParams();
			std::string reply = target + " " + modes;
			if (!params.empty())
				reply += " " + params;
			sendReply(client, RPL_CHANNELMODEIS, reply);

			sendReply(client, RPL_CREATIONTIME,
					  target + " " + libcpp::str::to_string(chan->getCreationTime()));
			return;
		}
		handleChannelMode(client, chan, msg);
	}
	else
	{
		// User mode
		handleUserMode(client, msg);
	}
}

/* ─── User Mode Handler ─── */

void Server::handleUserMode(Client *client, const Message &msg)
{
	const std::string &target = msg.params[0];

	// Can only query/change your own modes
	if (target != client->getNickname())
	{
		sendReply(client, ERR_USERSDONTMATCH,
				  ":Can't change mode for other users");
		return;
	}

	if (msg.params.size() == 1)
	{
		// Query mode
		sendReply(client, RPL_UMODEIS, "+");
		return;
	}

	// We don't support user modes — silently ignore or echo
	// HexChat sends MODE nick +i; we just acknowledge it
	const std::string &modeStr = msg.params[1];
	(void)modeStr;
	// Silently accept — don't error out
}

/* ─── Channel Mode Handler ─── */

void Server::handleChannelMode(Client *client, Channel *channel,
							   const Message &msg)
{
	if (!channel->isMember(client))
	{
		sendReply(client, ERR_NOTONCHANNEL,
				  channel->getName() + " :You're not on that channel");
		return;
	}

	if (!channel->isOperator(client))
	{
		sendReply(client, ERR_CHANOPRIVSNEEDED,
				  channel->getName() + " :You're not channel operator");
		return;
	}

	const std::string &modeStr = msg.params[1];
	bool adding = true;
	size_t paramIdx = 2;

	std::string appliedModes;
	std::string appliedParams;
	bool currentSign = true; // true = +

	for (size_t i = 0; i < modeStr.size(); ++i)
	{
		char c = modeStr[i];

		if (c == '+')
		{
			adding = true;
			continue;
		}
		if (c == '-')
		{
			adding = false;
			continue;
		}

		switch (c)
		{
			case 'i':
			{
				channel->setInviteOnly(adding);
				if (adding != currentSign || appliedModes.empty())
				{
					appliedModes += (adding ? "+" : "-");
					currentSign = adding;
				}
				appliedModes += "i";
				break;
			}
			case 't':
			{
				channel->setTopicRestricted(adding);
				if (adding != currentSign || appliedModes.empty())
				{
					appliedModes += (adding ? "+" : "-");
					currentSign = adding;
				}
				appliedModes += "t";
				break;
			}
			case 'k':
			{
				if (adding)
				{
					if (paramIdx >= msg.params.size())
					{
						sendReply(client, ERR_NEEDMOREPARAMS,
								  "MODE :Not enough parameters");
						continue;
					}
					std::string key = msg.params[paramIdx++];
					if (!isValidChannelKey(key))
					{
						sendReply(client, ERR_INVALIDKEY,
								  channel->getName() + " :Key is not well-formed");
						continue;
					}
					channel->setKey(key);
					if (adding != currentSign || appliedModes.empty())
					{
						appliedModes += "+";
						currentSign = true;
					}
					appliedModes += "k";
					if (!appliedParams.empty())
						appliedParams += " ";
					appliedParams += key;
				}
				else
				{
					channel->removeKey();
					if (adding != currentSign || appliedModes.empty())
					{
						appliedModes += "-";
						currentSign = false;
					}
					appliedModes += "k";
					// Some servers expect * as param for -k
					if (paramIdx < msg.params.size())
						paramIdx++;
				}
				break;
			}
			case 'o':
			{
				if (paramIdx >= msg.params.size())
				{
					sendReply(client, ERR_NEEDMOREPARAMS,
							  "MODE :Not enough parameters");
					continue;
				}
				std::string nick = msg.params[paramIdx++];
				Client *target = channel->findMember(nick);
				if (!target)
				{
					sendReply(client, ERR_USERNOTINCHANNEL,
							  nick + " " + channel->getName()
							  + " :They aren't on that channel");
					continue;
				}
				channel->setOperator(target, adding);
				if (adding != currentSign || appliedModes.empty())
				{
					appliedModes += (adding ? "+" : "-");
					currentSign = adding;
				}
				appliedModes += "o";
				if (!appliedParams.empty())
					appliedParams += " ";
				appliedParams += nick;
				break;
			}
			case 'l':
			{
				if (adding)
				{
					if (paramIdx >= msg.params.size())
					{
						sendReply(client, ERR_NEEDMOREPARAMS,
								  "MODE :Not enough parameters");
						continue;
					}
					std::string limitStr = msg.params[paramIdx++];
					/* Full-string strtol parse with bounds: rejects "12abc",
					** negatives, and values that overflow size_t when cast. */
					errno = 0;
					char *end = NULL;
					long limit = std::strtol(limitStr.c_str(), &end, 10);
					if (errno == ERANGE || end == limitStr.c_str() || *end != '\0'
						|| limit <= 0 || limit > MAX_USERLIMIT)
						continue;
					channel->setUserLimit(static_cast<size_t>(limit));
					if (adding != currentSign || appliedModes.empty())
					{
						appliedModes += "+";
						currentSign = true;
					}
					appliedModes += "l";
					if (!appliedParams.empty())
						appliedParams += " ";
					/* Echo the canonical parsed number, not the raw text. */
					appliedParams += libcpp::str::to_string(limit);
				}
				else
				{
					channel->removeUserLimit();
					if (adding != currentSign || appliedModes.empty())
					{
						appliedModes += "-";
						currentSign = false;
					}
					appliedModes += "l";
				}
				break;
			}
			default:
			{
				std::string s(1, c);
				sendReply(client, ERR_UNKNOWNMODE,
						  s + " :is unknown mode char to me");
				break;
			}
		}
	}

	// Broadcast applied modes
	if (!appliedModes.empty())
	{
		std::string modeMsg = ":" + client->getPrefix() + " MODE "
							  + channel->getName() + " " + appliedModes;
		if (!appliedParams.empty())
			modeMsg += " " + appliedParams;
		channel->broadcastMessage(modeMsg, NULL);
	}
}
