/* ─── MODE parsing matrix: signs, cumulation, parameter order, density ───
 *
 * wiki/FT_IRC_CLIENT_PROTOCOL/signatures.md gives MODE as
 *
 *     MODE <target> <modes> [parameters...]
 *
 * and the mode string is the part with a shape of its own. Two rules govern
 * it, and everything in this file is a consequence of one of them:
 *
 *   1. A mode string OPENS WITH A SIGN. "+i", "-o", "-o+i-t" are mode
 *      strings; "i", "it", "o nick" are not. There is no implicit "+", so an
 *      unsigned string applies nothing and answers nothing.
 *
 *   2. Past that sign, modes CUMULATE and the sign may flip mid-string:
 *      "+ii", "-oo", "-oi", "-o+i-t", "+ikl key 5". Each letter is applied in
 *      the order written, with the sign in force at that point, and each
 *      parameter-taking letter draws the next positional parameter in the
 *      order the letters appear -- so "+ko key nick" and "+ok nick key"
 *      differ only in which parameter goes where.
 *
 * The tables below are driven case-by-case against a live server: one fresh
 * channel per case (so no case can inherit another's state), then for each
 * case the echo broadcast to the channel, the resulting channel state read
 * back with a MODE query, and the exact multiset of error numerics are all
 * compared against what the two rules above predict.
 *
 * The five channel modes this server implements are i, t, k, l and o
 * (RPL_MYINFO "o itkol", ISUPPORT "CHANMODES=,,kl,it").
 */

#include <gtest/gtest.h>
#include "TestHarness.hpp"
#include "Replies.hpp"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

class ModeMatrixTest : public IrcServerTest
{
protected:
	int portBase() const override { return 17800; }
};

/* ══════════════════════════════════════════════════════════════════════════
 * Transcript plumbing
 * ══════════════════════════════════════════════════════════════════════ */

static std::vector<std::string> splitLines(const std::string &blob)
{
	std::vector<std::string> lines;
	std::string::size_type start = 0;
	while (start < blob.size())
	{
		std::string::size_type end = blob.find("\r\n", start);
		if (end == std::string::npos)
			break;
		lines.push_back(blob.substr(start, end - start));
		start = end + 2;
	}
	return lines;
}

static std::string itos(int n)
{
	std::ostringstream os;
	os << n;
	return os.str();
}

/* The numeric of ":server 461 nick MODE :Not enough parameters" — "" when
 * the line is not a numeric reply (a MODE echo, a PONG, a JOIN...). */
static std::string numericOf(const std::string &line)
{
	if (line.empty() || line[0] != ':')
		return "";
	std::string::size_type sp = line.find(' ');
	if (sp == std::string::npos)
		return "";
	std::string::size_type sp2 = line.find(' ', sp + 1);
	if (sp2 == std::string::npos)
		return "";
	std::string tok = line.substr(sp + 1, sp2 - sp - 1);
	if (tok.size() != 3)
		return "";
	for (size_t i = 0; i < 3; ++i)
		if (tok[i] < '0' || tok[i] > '9')
			return "";
	return tok;
}

/* Everything after "MODE <chan> " in a server-to-client MODE broadcast. */
static bool modeEchoTail(const std::string &line, const std::string &chan,
						 std::string &tail)
{
	const std::string needle = " MODE " + chan + " ";
	std::string::size_type at = line.find(needle);
	if (at == std::string::npos || line.empty() || line[0] != ':')
		return false;
	tail = line.substr(at + needle.size());
	return true;
}

/* Everything after "<chan> " in an RPL_CHANNELMODEIS. */
static bool modeIsTail(const std::string &line, const std::string &chan,
					   std::string &tail)
{
	if (numericOf(line) != RPL_CHANNELMODEIS)
		return false;
	const std::string needle = " " + chan + " ";
	std::string::size_type at = line.find(needle);
	if (at == std::string::npos)
		return false;
	tail = line.substr(at + needle.size());
	return true;
}

/* ══════════════════════════════════════════════════════════════════════════
 * The case table
 * ══════════════════════════════════════════════════════════════════════ */

struct ModeCase
{
	/* What follows "MODE <chan> " — mode string plus its parameters. */
	const char *send;
	/* Expected echo, as the tail after "MODE <chan> ". "" = no echo at all. */
	const char *echo;
	/* Expected RPL_CHANNELMODEIS tail afterwards. "+" = no modes set. */
	const char *state;
	/* Expected error numerics, space-separated, sorted. "" = none. */
	const char *errs;
	const char *why;
};

/* Observed result for one case, in the same shape. */
struct ModeResult
{
	std::vector<std::string> echoes;
	std::string state;
	std::vector<std::string> errs;
};

static std::string join(const std::vector<std::string> &v)
{
	std::string out;
	for (size_t i = 0; i < v.size(); ++i)
	{
		if (i)
			out += " ";
		out += v[i];
	}
	return out;
}

static std::vector<std::string> splitWords(const std::string &s)
{
	std::vector<std::string> out;
	std::istringstream is(s);
	std::string w;
	while (is >> w)
		out.push_back(w);
	return out;
}

/* Runs a whole table against one server.
 *
 * Every case gets its own channel, and all the joins happen up front so the
 * three clients are settled before a single MODE is sent. The MODE commands
 * then go out as one burst from the operator, each case fenced by a PING
 * whose token comes back as a PONG — that PONG is what slices the single
 * received transcript back into per-case chunks, with no per-case sleep. */
static void runCases(int port, const std::string &tag, const ModeCase *cases,
					 size_t n)
{
	TestClient op, bob, carol;
	ASSERT_TRUE(op.connect(port));
	ASSERT_TRUE(bob.connect(port));
	ASSERT_TRUE(carol.connect(port));

	op.registerClient("testpass", tag + "op", "opu");
	bob.registerClient("testpass", "bob", "bobu");
	carol.registerClient("testpass", "carol", "carolu");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	op.recvAll(200);
	bob.recvAll(200);
	carol.recvAll(200);

	std::vector<std::string> chans;
	for (size_t i = 0; i < n; ++i)
		chans.push_back("#" + tag + itos(static_cast<int>(i)));

	/* op joins first everywhere, so op is the channel operator everywhere. */
	for (size_t i = 0; i < n; ++i)
		op.sendCmd("JOIN " + chans[i]);
	std::this_thread::sleep_for(std::chrono::milliseconds(250));
	op.recvAll(250);

	for (size_t i = 0; i < n; ++i)
	{
		bob.sendCmd("JOIN " + chans[i]);
		carol.sendCmd("JOIN " + chans[i]);
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(350));
	op.recvAll(300);
	bob.recvAll(200);
	carol.recvAll(200);

	for (size_t i = 0; i < n; ++i)
	{
		op.sendCmd("MODE " + chans[i] + " " + cases[i].send);
		op.sendCmd("MODE " + chans[i]);
		op.sendCmd("PING :fence" + itos(static_cast<int>(i)));
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(400));
	std::vector<std::string> lines = splitLines(op.recvAll(600));

	/* Slice the transcript on the PONG fences. */
	std::vector<ModeResult> got(n);
	size_t cur = 0;
	for (size_t li = 0; li < lines.size() && cur < n; ++li)
	{
		const std::string &line = lines[li];

		if (line.find(":fence" + itos(static_cast<int>(cur))) != std::string::npos &&
			line.find(" PONG ") != std::string::npos)
		{
			++cur;
			continue;
		}

		std::string tail;
		if (modeEchoTail(line, chans[cur], tail))
			got[cur].echoes.push_back(tail);
		else if (modeIsTail(line, chans[cur], tail))
			got[cur].state = tail;
		else
		{
			std::string num = numericOf(line);
			/* RPL_CREATIONTIME rides along with every mode query. */
			if (!num.empty() && num != RPL_CHANNELMODEIS && num != RPL_CREATIONTIME)
				got[cur].errs.push_back(num);
		}
	}
	ASSERT_EQ(cur, n) << "transcript ended after " << cur << "/" << n
					  << " fences — the server stopped answering";

	for (size_t i = 0; i < n; ++i)
	{
		const ModeCase &c = cases[i];
		const std::string label = "MODE " + chans[i] + " " + c.send +
								  "\n      (" + c.why + ")";

		EXPECT_EQ(join(got[i].echoes), std::string(c.echo))
			<< "wrong echo for " << label;
		EXPECT_EQ(got[i].state, std::string(c.state))
			<< "wrong resulting channel state for " << label;

		std::vector<std::string> wantErrs = splitWords(c.errs);
		std::vector<std::string> haveErrs = got[i].errs;
		std::sort(wantErrs.begin(), wantErrs.end());
		std::sort(haveErrs.begin(), haveErrs.end());
		EXPECT_EQ(join(haveErrs), join(wantErrs))
			<< "wrong error numerics for " << label;
	}

	op.sendCmd("QUIT");
	bob.sendCmd("QUIT");
	carol.sendCmd("QUIT");
}

/* ══════════════════════════════════════════════════════════════════════════
 * Rule 1 — a mode string that does not open with a sign does nothing
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(ModeMatrixTest, UnsignedModeStringIsInert)
{
	static const ModeCase cases[] = {
		{"i", "", "+", "", "a bare letter is not a mode string"},
		{"t", "", "+", "", "same for every implemented flag"},
		{"it", "", "+", "", "cumulation does not rescue a missing sign"},
		{"o bob", "", "+", "", "nor does a parameter-taking mode"},
		{"k secret", "", "+", "", "the key must not be set either"},
		{"l 5", "", "+", "", "nor the limit"},
		{"itkol bob secret 5", "", "+", "", "the whole set, still inert"},
		{"x", "", "+", "", "an unsigned unknown char is not answered 472"},
		{"xyz", "", "+", "", "no 472 storm from unsigned junk either"},
		{"1+i", "", "+", "", "the sign has to be FIRST, not merely present"},
		{"i+t", "", "+", "", "a later sign does not retroactively open"},
		{"*", "", "+", "", "punctuation is not a sign"},
	};
	runCases(serverPort, "sig", cases, sizeof(cases) / sizeof(cases[0]));
}

/* ══════════════════════════════════════════════════════════════════════════
 * Rule 2a — cumulation under a single sign
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(ModeMatrixTest, ModesCumulateUnderOneSign)
{
	static const ModeCase cases[] = {
		{"+i", "+i", "+i", "", "the minimal mode string"},
		{"+ii", "+ii", "+i", "", "a repeat applies twice and is echoed twice"},
		{"+iii", "+iii", "+i", "", "and stays idempotent in the state"},
		{"-ii", "-ii", "+", "", "-ii on an unset flag is a legal no-change"},
		{"+it", "+it", "+it", "", "two flags, one sign"},
		{"+ti", "+ti", "+it", "", "input order is free; 324 is canonical"},
		{"+iitt", "+iitt", "+it", "", "repeats of several flags"},
		{"+oo bob carol", "+oo bob carol", "+", "",
		 "one parameter per o, in order"},
		{"-oo bob carol", "-oo bob carol", "+", "",
		 "-oo also draws one parameter each"},
		{"+oi bob", "+oi bob", "+i", "", "parameterised and flag modes mix"},
		{"-oi bob", "-oi bob", "+", "", "the -oi form from the signatures"},
		{"+ikl secret 5", "+ikl secret 5", "+ikl secret 5", "",
		 "k and l draw parameters, i does not"},
		{"+itkl secret 5", "+itkl secret 5", "+itkl secret 5", "",
		 "every settable mode at once"},
		{"+itkol secret bob 5", "+itkol secret bob 5", "+itkl secret 5", "",
		 "…including o, whose parameter sits between k's and l's"},
	};
	runCases(serverPort, "cum", cases, sizeof(cases) / sizeof(cases[0]));
}

/* ══════════════════════════════════════════════════════════════════════════
 * Rule 2b — the sign may flip mid-string
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(ModeMatrixTest, SignFlipsMidString)
{
	static const ModeCase cases[] = {
		{"+i-t", "+i-t", "+i", "", "one flip"},
		{"-o+i-t bob", "-o+i-t bob", "+i", "",
		 "the -o+i-t form from the signatures"},
		{"+i-i", "+i-i", "+", "", "set then unset in one command"},
		{"-i+i", "-i+i", "+i", "", "and the other way round"},
		{"+i+i", "+ii", "+i", "", "a redundant sign is not restated in the echo"},
		{"+i+t", "+it", "+it", "", "same when the letters differ"},
		{"-i-t", "-it", "+", "", "and under a negative run"},
		{"++i", "+i", "+i", "", "a doubled sign is just the sign"},
		{"--i", "-i", "+", "", "likewise negative"},
		{"+-i", "-i", "+", "", "the last sign before a letter wins"},
		{"-+i", "+i", "+i", "", "and again the other way"},
		{"+-+-i", "-i", "+", "", "a run of signs collapses to the last"},
		{"+", "", "+", "", "a lone sign applies nothing and is silent"},
		{"-", "", "+", "", "same for the negative"},
		{"+-+-", "", "+", "", "signs with no letters at all"},
		{"+o-o bob bob", "+o-o bob bob", "+", "",
		 "grant then revoke, each drawing its own parameter"},
		{"+o-o+o bob bob bob", "+o-o+o bob bob bob", "+", "",
		 "three parameters for three o's"},
		{"+t-i+t-i", "+t-i+t-i", "+t", "", "alternating signs, repeated flags"},
	};
	runCases(serverPort, "flip", cases, sizeof(cases) / sizeof(cases[0]));
}

/* ══════════════════════════════════════════════════════════════════════════
 * Parameter binding — which letter takes which positional parameter
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(ModeMatrixTest, ParametersBindToLettersInOrder)
{
	static const ModeCase cases[] = {
		{"+ko secret bob", "+ko secret bob", "+k secret", "",
		 "k first, so k takes the first parameter"},
		{"+ok bob secret", "+ok bob secret", "+k secret", "",
		 "swap the letters and the parameters swap with them"},
		{"+lo 5 bob", "+lo 5 bob", "+l 5", "", "l before o"},
		{"+ol bob 5", "+ol bob 5", "+l 5", "", "o before l"},
		{"+kl secret 5", "+kl secret 5", "+kl secret 5", "", "k then l"},
		{"+lk 5 secret", "+lk 5 secret", "+kl secret 5", "",
		 "l then k — 324 still lists key before limit"},
		{"-l+i", "-l+i", "+i", "", "-l takes no parameter"},
		{"+l-i 5", "+l-i 5", "+l 5", "", "+l does, and -i does not"},
		{"-k+o bob", "-k+o bob", "+", "",
		 "-k must not eat the parameter +o needs"},
		{"-kl+o bob", "-kl+o bob", "+", "",
		 "…nor when -l sits between them"},
		{"+o-i-o bob bob", "+o-io bob bob", "+", "",
		 "an unparameterised mode between two o's does not shift the binding "
		 "— and the echo does not restate the sign it is already under"},
		{"+oo bob bob", "+oo bob bob", "+", "",
		 "the same nick twice is two grants, not one"},
	};
	runCases(serverPort, "bind", cases, sizeof(cases) / sizeof(cases[0]));
}

/* ══════════════════════════════════════════════════════════════════════════
 * Errors — one reply per distinct complaint, and valid modes still apply
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(ModeMatrixTest, ErrorsAreReportedOncePerDistinctComplaint)
{
	static const ModeCase cases[] = {
		{"+k", "", "+", ERR_NEEDMOREPARAMS, "+k with no key"},
		{"+kk", "", "+", ERR_NEEDMOREPARAMS,
		 "one shortfall is a property of the command, not of each letter"},
		{"+o", "", "+", ERR_NEEDMOREPARAMS, "+o with no nick"},
		{"+ooo", "", "+", ERR_NEEDMOREPARAMS, "three starved o's, one 461"},
		{"+l", "", "+", ERR_NEEDMOREPARAMS, "+l with no limit"},
		{"+klo", "", "+", ERR_NEEDMOREPARAMS,
		 "three different starved modes, still one 461"},
		{"+x", "", "+", ERR_UNKNOWNMODE, "an unknown char under a sign IS 472"},
		{"+xx", "", "+", ERR_UNKNOWNMODE, "repeated, it is still one complaint"},
		{"+xy", "", "+", "472 472", "two distinct chars are two complaints"},
		{"+jfsadfsahf", "", "+", "472 472 472 472 472 472",
		 "ten chars, six distinct (j f s a d h)"},
		{"+o nosuchnick", "", "+", ERR_USERNOTINCHANNEL, "441 for a stranger"},
		{"+oo nosuchnick nosuchnick", "", "+", ERR_USERNOTINCHANNEL,
		 "the same stranger twice is one complaint"},
		{"+oo nosuchnick otherghost", "", "+", "441 441",
		 "two strangers are two"},
		{"+l abc", "", "+", ERR_INVALIDMODEPARAM, "a limit must be a number"},
		{"+l 0", "", "+", ERR_INVALIDMODEPARAM, "…at least 1"},
		{"+l -3", "", "+", ERR_INVALIDMODEPARAM, "…and not negative"},
		{"+l 65536", "", "+", ERR_INVALIDMODEPARAM, "…and at most MAX_USERLIMIT"},
		{"+ll abc abc", "", "+", ERR_INVALIDMODEPARAM,
		 "the same bad limit twice is one complaint"},
		{"+ll abc def", "", "+", "696 696", "two bad limits are two"},
		{"+k bad,key", "", "+", ERR_INVALIDKEY, "a comma cannot be in a key"},
		{"+kk bad,key bad,key", "", "+", ERR_INVALIDKEY,
		 "the same bad key twice is one complaint"},
		{"+i+x", "+i", "+i", ERR_UNKNOWNMODE,
		 "a bad char does not discard the good modes around it"},
		{"+xi", "+i", "+i", ERR_UNKNOWNMODE, "…in either order"},
		{"+ox bob", "+o bob", "+", ERR_UNKNOWNMODE,
		 "…nor a good grant before it"},
		{"+ko secret", "+k secret", "+k secret", ERR_NEEDMOREPARAMS,
		 "k consumed the only parameter; o is starved but k still stands"},
		{"+abcdefghijklmnopqrstuvwxyz", "+it", "+it",
		 "461 472 472 472 472 472 472 472 472 472 472 472 472 472 472 472 472 "
		 "472 472 472 472 472",
		 "every letter at once: i and t apply, k/l/o starve into one 461, "
		 "and the 21 remaining letters are 21 distinct 472s"},
	};
	runCases(serverPort, "err", cases, sizeof(cases) / sizeof(cases[0]));
}

/* ══════════════════════════════════════════════════════════════════════════
 * Stateful cases — what a mode string does depends on where it starts
 * ══════════════════════════════════════════════════════════════════════ */

/* Sends one command from `op` and returns everything that came back. */
static std::string step(TestClient &op, const std::string &cmd)
{
	op.sendCmd(cmd);
	std::this_thread::sleep_for(std::chrono::milliseconds(150));
	return op.recvAll(250);
}

TEST_F(ModeMatrixTest, RemoveKeyEchoesTheKeyOnlyWhenItIsSurplus)
{
	TestClient op, bob;
	ASSERT_TRUE(op.connect(serverPort));
	ASSERT_TRUE(bob.connect(serverPort));
	op.registerClient("testpass", "keyop", "keyop");
	bob.registerClient("testpass", "keybob", "keybob");
	std::this_thread::sleep_for(std::chrono::milliseconds(150));
	op.recvAll(200);
	bob.recvAll(200);

	op.sendCmd("JOIN #keys");
	std::this_thread::sleep_for(std::chrono::milliseconds(150));
	bob.sendCmd("JOIN #keys");
	std::this_thread::sleep_for(std::chrono::milliseconds(250));
	op.recvAll(200);
	bob.recvAll(200);

	/* -k with a surplus argument: RFC 2812 spells -k with the key and
	** clients send one, so it is consumed — and echoed, because a consumed
	** parameter missing from the broadcast is indistinguishable from one
	** that was never sent. */
	step(op, "MODE #keys +k oldkey");
	std::string got = step(op, "MODE #keys -k oldkey");
	EXPECT_NE(got.find("MODE #keys -k oldkey"), std::string::npos)
		<< "a surplus -k argument must be echoed, got:\n" << got;

	/* -k with no argument at all: nothing to echo, and no error either. */
	step(op, "MODE #keys +k secondkey");
	got = step(op, "MODE #keys -k");
	EXPECT_NE(got.find("MODE #keys -k"), std::string::npos)
		<< "-k on its own must still remove the key, got:\n" << got;
	EXPECT_EQ(got.find(ERR_NEEDMOREPARAMS), std::string::npos)
		<< "-k needs no argument, got:\n" << got;

	/* -k followed by a mode that needs the parameter: +o gets it. */
	step(op, "MODE #keys +k thirdkey");
	got = step(op, "MODE #keys -k+o keybob");
	EXPECT_NE(got.find("MODE #keys -k+o keybob"), std::string::npos)
		<< "-k must leave the parameter for +o, got:\n" << got;

	/* And the channel really is keyless and keybob really is an operator. */
	got = step(op, "MODE #keys");
	EXPECT_NE(got.find(" " + std::string(RPL_CHANNELMODEIS) + " "), std::string::npos);
	EXPECT_EQ(got.find("thirdkey"), std::string::npos)
		<< "the key survived -k, got:\n" << got;

	got = step(bob, "MODE #keys +t");
	EXPECT_NE(got.find("MODE #keys +t"), std::string::npos)
		<< "keybob never actually received +o, got:\n" << got;

	op.sendCmd("QUIT");
	bob.sendCmd("QUIT");
}

TEST_F(ModeMatrixTest, ReplacingAndReversingModesAcrossCommands)
{
	TestClient op;
	ASSERT_TRUE(op.connect(serverPort));
	op.registerClient("testpass", "stateop", "stateop");
	std::this_thread::sleep_for(std::chrono::milliseconds(150));
	op.recvAll(200);

	op.sendCmd("JOIN #state");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	op.recvAll(200);

	step(op, "MODE #state +itkl first 10");
	std::string got = step(op, "MODE #state");
	EXPECT_NE(got.find("#state +itkl first 10"), std::string::npos)
		<< "all four settable modes should be listed, got:\n" << got;

	/* A second +k replaces the key rather than being refused. */
	step(op, "MODE #state +k second");
	got = step(op, "MODE #state");
	EXPECT_NE(got.find("#state +itkl second 10"), std::string::npos)
		<< "+k on a keyed channel must replace the key, got:\n" << got;

	/* One command that reverses everything set so far. */
	got = step(op, "MODE #state -itkl second");
	EXPECT_NE(got.find("MODE #state -itkl second"), std::string::npos)
		<< "the reversing echo is wrong, got:\n" << got;
	got = step(op, "MODE #state");
	EXPECT_NE(got.find("#state +\r\n"), std::string::npos)
		<< "the channel should be back to no modes, got:\n" << got;

	op.sendCmd("QUIT");
}

/* ══════════════════════════════════════════════════════════════════════════
 * Authorisation — checked before the mode string is even looked at
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(ModeMatrixTest, AuthorisationIsAnsweredBeforeTheModeStringIsParsed)
{
	TestClient op, member, outsider;
	ASSERT_TRUE(op.connect(serverPort));
	ASSERT_TRUE(member.connect(serverPort));
	ASSERT_TRUE(outsider.connect(serverPort));
	op.registerClient("testpass", "authop", "authop");
	member.registerClient("testpass", "authmem", "authmem");
	outsider.registerClient("testpass", "authout", "authout");
	std::this_thread::sleep_for(std::chrono::milliseconds(150));
	op.recvAll(200);
	member.recvAll(200);
	outsider.recvAll(200);

	op.sendCmd("JOIN #auth");
	std::this_thread::sleep_for(std::chrono::milliseconds(150));
	member.sendCmd("JOIN #auth");
	std::this_thread::sleep_for(std::chrono::milliseconds(250));
	op.recvAll(200);
	member.recvAll(200);

	/* A member who is not an operator is refused — for a well-formed mode
	** string and for an unsigned one alike. 482 answers "may you touch this
	** channel's modes", which is true or false whatever the string says. */
	std::string got = step(member, "MODE #auth +i");
	EXPECT_NE(got.find(ERR_CHANOPRIVSNEEDED), std::string::npos)
		<< "a non-operator must get 482, got:\n" << got;
	got = step(member, "MODE #auth i");
	EXPECT_NE(got.find(ERR_CHANOPRIVSNEEDED), std::string::npos)
		<< "482 comes before the sign rule, got:\n" << got;

	/* A non-member gets 442, again either way. */
	got = step(outsider, "MODE #auth +i");
	EXPECT_NE(got.find(ERR_NOTONCHANNEL), std::string::npos)
		<< "a non-member must get 442, got:\n" << got;
	got = step(outsider, "MODE #auth it");
	EXPECT_NE(got.find(ERR_NOTONCHANNEL), std::string::npos)
		<< "442 comes before the sign rule too, got:\n" << got;

	/* An unknown channel is 403 regardless of the mode string. */
	got = step(op, "MODE #nosuchchan +i");
	EXPECT_NE(got.find(ERR_NOSUCHCHANNEL), std::string::npos)
		<< "an unknown channel must be 403, got:\n" << got;
	got = step(op, "MODE #nosuchchan i");
	EXPECT_NE(got.find(ERR_NOSUCHCHANNEL), std::string::npos)
		<< "403 comes before the sign rule, got:\n" << got;

	/* MODE with no target at all. */
	got = step(op, "MODE");
	EXPECT_NE(got.find(ERR_NEEDMOREPARAMS), std::string::npos)
		<< "a bare MODE must be 461, got:\n" << got;

	/* Nothing above may have leaked a mode onto the channel. */
	got = step(op, "MODE #auth");
	EXPECT_NE(got.find("#auth +\r\n"), std::string::npos)
		<< "a refused MODE still changed the channel, got:\n" << got;

	op.sendCmd("QUIT");
	member.sendCmd("QUIT");
	outsider.sendCmd("QUIT");
}

/* ══════════════════════════════════════════════════════════════════════════
 * Density — a mode string as long as a line will allow
 * ══════════════════════════════════════════════════════════════════════ */

static size_t longestLine(const std::string &blob)
{
	size_t worst = 0;
	std::string::size_type start = 0;
	while (start < blob.size())
	{
		std::string::size_type end = blob.find("\r\n", start);
		if (end == std::string::npos)
			break;
		size_t len = (end - start) + 2;
		if (len > worst)
			worst = len;
		start = end + 2;
	}
	return worst;
}

static size_t countLines(const std::string &blob)
{
	size_t n = 0;
	std::string::size_type start = 0;
	while ((start = blob.find("\r\n", start)) != std::string::npos)
	{
		++n;
		start += 2;
	}
	return n;
}

TEST_F(ModeMatrixTest, ADenseModeStringStaysWithinTheLineLimit)
{
	/* 480 alternating i/t under one sign: every one of them applies, so the
	** echo carries all 480 letters plus a prefix the request did not have.
	** On one line that is ~510+ bytes and Client::queueMessage() would cut
	** it, telling the channel about changes it cannot name. It has to be
	** split instead, and every piece has to be a legal line. */
	TestClient op, watcher;
	ASSERT_TRUE(op.connect(serverPort));
	ASSERT_TRUE(watcher.connect(serverPort));
	op.registerClient("testpass", "denseop", "denseop");
	watcher.registerClient("testpass", "densewat", "densewat");
	std::this_thread::sleep_for(std::chrono::milliseconds(150));
	op.recvAll(200);
	watcher.recvAll(200);

	op.sendCmd("JOIN #dense");
	std::this_thread::sleep_for(std::chrono::milliseconds(150));
	watcher.sendCmd("JOIN #dense");
	std::this_thread::sleep_for(std::chrono::milliseconds(250));
	op.recvAll(200);
	watcher.recvAll(200);

	std::string dense = "+";
	for (int i = 0; i < 240; ++i)
		dense += "it";

	op.sendCmd("MODE #dense " + dense);
	std::this_thread::sleep_for(std::chrono::milliseconds(300));
	std::string seen = watcher.recvAll(400);

	EXPECT_LE(longestLine(seen), static_cast<size_t>(MAX_MSGLEN))
		<< "a dense MODE echo overran the 512-byte line limit";
	EXPECT_NE(seen.find(" MODE #dense "), std::string::npos)
		<< "the dense MODE was never echoed at all";

	/* Split, not truncated: the pieces together must carry every letter. */
	size_t letters = 0;
	for (size_t i = 0; i < seen.size(); ++i)
		if (seen[i] == 'i' || seen[i] == 't')
			++letters;
	EXPECT_GE(letters, 480u)
		<< "the echo lost mode letters — it was truncated, not split";

	/* And the state is just +it, however many times it was applied. */
	std::string got = step(op, "MODE #dense");
	EXPECT_NE(got.find("#dense +it"), std::string::npos)
		<< "480 applications should still leave exactly +it, got:\n" << got;

	op.sendCmd("QUIT");
	watcher.sendCmd("QUIT");
}

TEST_F(ModeMatrixTest, ADenseJunkModeStringCannotStormTheSender)
{
	/* The same length, but all junk: the reply budget is bounded by the
	** number of DISTINCT unknown characters (95 printable ASCII at the very
	** most), not by the length of the string. */
	TestClient op;
	ASSERT_TRUE(op.connect(serverPort));
	op.registerClient("testpass", "stormy", "stormy");
	std::this_thread::sleep_for(std::chrono::milliseconds(150));
	op.recvAll(200);

	op.sendCmd("JOIN #stormy");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	op.recvAll(200);

	std::string junk = "+";
	while (junk.size() < 480)
		junk += "zqwy";

	op.sendCmd("MODE #stormy " + junk);
	std::this_thread::sleep_for(std::chrono::milliseconds(400));
	std::string got = op.recvAll(500);

	EXPECT_EQ(countLines(got), 4u)
		<< "expected one 472 per distinct char (z q w y), got:\n" << got;
	EXPECT_LE(longestLine(got), static_cast<size_t>(MAX_MSGLEN));

	op.sendCmd("QUIT");
}

/* ══════════════════════════════════════════════════════════════════════════
 * Mode letters are case-sensitive
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(ModeMatrixTest, UppercaseModeLettersAreDistinctAndUnknown)
{
	/* CASEMAPPING=ascii folds nicks and channel names, not mode letters.
	 * ISUPPORT advertises "CHANMODES=,,kl,it" in lower case and that is the
	 * whole set — "+I" is not "+i", it is an unknown mode char. */
	static const ModeCase cases[] = {
		{"+I", "", "+", ERR_UNKNOWNMODE, "+I is not +i"},
		{"+T", "", "+", ERR_UNKNOWNMODE, "+T is not +t"},
		{"+K key", "", "+", ERR_UNKNOWNMODE, "+K is not +k, and eats no key"},
		{"+L 5", "", "+", ERR_UNKNOWNMODE, "+L is not +l"},
		{"+O bob", "", "+", ERR_UNKNOWNMODE, "+O is not +o"},
		{"+iI", "+i", "+i", ERR_UNKNOWNMODE,
		 "the lower-case half still applies"},
		{"+Ii", "+i", "+i", ERR_UNKNOWNMODE, "…in either order"},
		{"+iItT", "+it", "+it", "472 472",
		 "two unknown upper-case letters, two complaints"},
	};
	runCases(serverPort, "case", cases, sizeof(cases) / sizeof(cases[0]));
}

/* ══════════════════════════════════════════════════════════════════════════
 * Parameter values that look like syntax
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(ModeMatrixTest, ParametersAreNeverReparsedAsModeCharacters)
{
	/* A parameter is a value, not more mode string. A key of "+i" sets the
	 * key to the two characters '+' and 'i'; it does not make the channel
	 * invite-only. Anything else would let a key choose the channel's modes. */
	static const ModeCase cases[] = {
		{"+k +i", "+k +i", "+k +i", "", "a key may look like a mode string"},
		{"+k -o", "+k -o", "+k -o", "", "…including a negative one"},
		{"+k +itkol", "+k +itkol", "+k +itkol", "",
		 "…even the whole implemented set"},
		{"+ki +i", "+ki +i", "+ik +i", "",
		 "the +i AFTER the key is the mode; the +i parameter is not"},
		{"+o +i", "", "+", ERR_USERNOTINCHANNEL,
		 "a nick-shaped parameter is looked up as a nick, not parsed"},
	};
	runCases(serverPort, "param", cases, sizeof(cases) / sizeof(cases[0]));
}

/* ══════════════════════════════════════════════════════════════════════════
 * Boundary values for the two parameterised value modes
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(ModeMatrixTest, LimitAndKeyBoundaries)
{
	const std::string maxKey(MAX_KEYLEN, 'k');
	const std::string tooLongKey(MAX_KEYLEN + 1, 'k');

	const std::string okMin = "+l 1";
	const std::string okMax = "+l 65535";
	const std::string setMin = "+l 1";
	const std::string setMax = "+l 65535";
	const std::string keyOk = "+k " + maxKey;
	const std::string keyBad = "+k " + tooLongKey;
	const std::string keyOkEcho = "+k " + maxKey;
	const std::string keyOkState = "+k " + maxKey;

	const ModeCase cases[] = {
		{okMin.c_str(), setMin.c_str(), "+l 1", "", "1 is the smallest limit"},
		{okMax.c_str(), setMax.c_str(), "+l 65535", "",
		 "MAX_USERLIMIT is the largest"},
		{"+l 65536", "", "+", ERR_INVALIDMODEPARAM, "one past it is refused"},
		{"+l 1x", "", "+", ERR_INVALIDMODEPARAM,
		 "a trailing non-digit is not silently dropped"},
		{"+l +5", "", "+", ERR_INVALIDMODEPARAM, "nor is a leading sign taken"},
		{"+l ", "", "+", ERR_NEEDMOREPARAMS,
		 "an empty limit is a missing parameter, not a zero one"},
		{keyOk.c_str(), keyOkEcho.c_str(), keyOkState.c_str(), "",
		 "a key of exactly MAX_KEYLEN is accepted"},
		{keyBad.c_str(), "", "+", ERR_INVALIDKEY, "one octet longer is not"},
		{"+k with space", "+k with", "+k with", "",
		 "the key stops at the space — 'space' is a separate parameter"},
	};
	runCases(serverPort, "bound", cases, sizeof(cases) / sizeof(cases[0]));
}

/* ══════════════════════════════════════════════════════════════════════════
 * User modes — MODE <nick>, which takes the other branch entirely
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(ModeMatrixTest, UserModeTargetsAreHandledSeparately)
{
	TestClient alice, bob;
	ASSERT_TRUE(alice.connect(serverPort));
	ASSERT_TRUE(bob.connect(serverPort));
	alice.registerClient("testpass", "alice", "aliceu");
	bob.registerClient("testpass", "bob2", "bob2u");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	alice.recvAll(200);
	bob.recvAll(200);

	/* Querying your own modes works, in any case — CASEMAPPING=ascii. */
	std::string got = step(alice, "MODE alice");
	EXPECT_NE(got.find(" " + std::string(RPL_UMODEIS) + " "), std::string::npos)
		<< "MODE <own nick> must answer 221, got:\n" << got;

	got = step(alice, "MODE ALICE");
	EXPECT_NE(got.find(" " + std::string(RPL_UMODEIS) + " "), std::string::npos)
		<< "the nick comparison must be case-insensitive, got:\n" << got;

	/* Someone else's modes are refused, whatever the mode string. */
	got = step(alice, "MODE bob2");
	EXPECT_NE(got.find(ERR_USERSDONTMATCH), std::string::npos)
		<< "querying another user's modes must be 502, got:\n" << got;

	got = step(alice, "MODE bob2 +i");
	EXPECT_NE(got.find(ERR_USERSDONTMATCH), std::string::npos)
		<< "setting another user's modes must be 502, got:\n" << got;

	/* This server implements no settable user modes (RPL_MYINFO lists "o"
	** for the oper mode only, which no command grants). A user mode string
	** aimed at yourself is therefore accepted and does nothing — the point
	** of the test is that it does not crash, does not leak a channel reply,
	** and does not change what a later query reports. */
	step(alice, "MODE alice +i");
	step(alice, "MODE alice -i+w");
	step(alice, "MODE alice i");
	got = step(alice, "MODE alice");
	EXPECT_NE(got.find(" " + std::string(RPL_UMODEIS) + " "), std::string::npos)
		<< "the user-mode query stopped working after a set attempt, got:\n"
		<< got;
	EXPECT_EQ(got.find(ERR_UNKNOWNMODE), std::string::npos)
		<< "a user-mode string must not be answered with a CHANNEL mode "
		   "error, got:\n" << got;

	/* An unknown nick is not a channel either. */
	got = step(alice, "MODE nosuchuser +i");
	EXPECT_NE(got.find(ERR_USERSDONTMATCH), std::string::npos)
		<< "an unknown nick target must not be treated as a channel, got:\n"
		<< got;

	alice.sendCmd("QUIT");
	bob.sendCmd("QUIT");
}

/* ══════════════════════════════════════════════════════════════════════════
 * Pipelining — several MODE commands arriving in one TCP segment
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(ModeMatrixTest, PipelinedModeCommandsApplyInOrder)
{
	/* Framing and mode parsing are separate concerns, and this is where they
	 * meet: four MODE commands in a single write() must be applied in the
	 * order written, exactly as if they had arrived one per segment. */
	TestClient op;
	ASSERT_TRUE(op.connect(serverPort));
	op.registerClient("testpass", "pipeop", "pipeop");
	std::this_thread::sleep_for(std::chrono::milliseconds(150));
	op.recvAll(200);

	op.sendCmd("JOIN #pipe");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	op.recvAll(200);

	op.sendRaw("MODE #pipe +i\r\n"
			   "MODE #pipe +t\r\n"
			   "MODE #pipe -i\r\n"
			   "MODE #pipe +kl secret 9\r\n");
	std::this_thread::sleep_for(std::chrono::milliseconds(350));
	std::string got = op.recvAll(400);

	EXPECT_NE(got.find("MODE #pipe +i"), std::string::npos) << got;
	EXPECT_NE(got.find("MODE #pipe +t"), std::string::npos) << got;
	EXPECT_NE(got.find("MODE #pipe -i"), std::string::npos) << got;
	EXPECT_NE(got.find("MODE #pipe +kl secret 9"), std::string::npos) << got;

	/* The net effect of the four, in order: t, k and l set; i set then unset. */
	got = step(op, "MODE #pipe");
	EXPECT_NE(got.find("#pipe +tkl secret 9"), std::string::npos)
		<< "pipelined MODEs did not compose in order, got:\n" << got;

	op.sendCmd("QUIT");
}
