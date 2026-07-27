#include "IrcCase.hpp"

/* The one definition of IRC's CASEMAPPING=ascii fold, shared by both
** entry points below. Deliberately ASCII-only: libcpp::str::eq_nocase is
** UTF-8 aware and would fold accented letters that the advertised ascii
** casemapping requires to stay distinct. */
static char ircFold(char c)
{
	if (c >= 'A' && c <= 'Z')
		return static_cast<char>(c - 'A' + 'a');
	return c;
}

std::string ircToLower(const std::string &s)
{
	std::string out(s);
	for (std::string::size_type i = 0; i < out.size(); ++i)
		out[i] = ircFold(out[i]);
	return out;
}

/* Folds in place rather than building two lowercase copies, and stops at the
** first differing character. This is the inner loop of every nick lookup:
** findClientByNick walks the whole client map for each PRIVMSG to a nick,
** INVITE, WHOIS, USERHOST and NICK, so the old form cost two heap
** allocations per client examined. */
bool ircEquals(const std::string &a, const std::string &b)
{
	if (a.size() != b.size())
		return false;
	for (std::string::size_type i = 0; i < a.size(); ++i)
	{
		if (ircFold(a[i]) != ircFold(b[i]))
			return false;
	}
	return true;
}
