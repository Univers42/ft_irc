#ifndef MESSAGE_HPP
# define MESSAGE_HPP

# include <string>
# include <vector>

/* A parsed IRC line. There is deliberately no `prefix` field: RFC 2812 §2.3
** says clients SHOULD NOT send one, this server has no server-to-server link
** that would make one meaningful, and nothing in src/ ever read it. parse()
** still *skips* a leading ":prefix " so it can't be mistaken for the command
** -- it just doesn't allocate a string to store what no one consults. */
struct Message
{
	std::string					command;
	std::vector<std::string>	params;

	static Message	parse(const std::string &raw);
};

#endif
