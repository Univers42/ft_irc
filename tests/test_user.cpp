/* ─── USER command: the registration parse, exhaustively ─────────────────
 *
 * wiki/FT_IRC_CLIENT_PROTOCOL/signatures.md gives the command as
 *
 *     USER <user> <mode> <unused> :<realname>
 *
 * Four parameters, each with a shape of its own, and the fourth one is a
 * TRAILING parameter — the colon is part of the grammar, not decoration.
 * Four rules follow, and every case below is a consequence of one of them:
 *
 *   1. ARITY AND FORM. Exactly four parameters, the fourth of which must be
 *      the trailing one. "USER a 0 * Real" has four parameters but no
 *      trailing, so the realname would silently become just its first word;
 *      "USER a 0 * x :y" has five, so the trailing is not the realname slot.
 *      Both are refused, and both refusals fall out of one check.
 *
 *   2. <user> is the RFC 2812 `user` production:
 *          user = 1*( %x01-09 / %x0B-0C / %x0E-1F / %x21-3F / %x41-FF )
 *      — any octet except NUL, LF, CR, SPACE and "@". The exclusion that
 *      matters in practice is "@": the prefix this server stamps on every
 *      relayed line is nick!user@host, so a username carrying an "@" makes
 *      the prefix ambiguous about where the host begins.
 *
 *   3. <mode> is a BITMASK (RFC 2812 §3.1.3): bit 2 (value 4) sets user mode
 *      w, bit 3 (value 8) sets user mode i. Other bits carry no meaning, and
 *      a parameter that is not a number carries no bits at all — it is
 *      ignored rather than refused, because the RFC says "should be a
 *      numeric" and refusing registration over a cosmetic field would be the
 *      worse failure.
 *
 *   4. <unused> has no meaning. Clients send "*"; anything is accepted; it
 *      simply has to be PRESENT, because it holds the realname's position.
 *
 * Each case registers a fresh connection and then reads its own identity
 * back off the wire with WHOIS, so what is asserted is what the server
 * actually stored, not merely that it did not complain.
 */

#include <gtest/gtest.h>
#include "TestHarness.hpp"
#include "Replies.hpp"

#include <sstream>
#include <string>
#include <vector>

class UserMatrixTest : public IrcServerTest
{
protected:
	int portBase() const override { return 17900; }
};

/* ══════════════════════════════════════════════════════════════════════════
 * One registration attempt, and what came of it
 * ══════════════════════════════════════════════════════════════════════ */

struct Registration
{
	bool welcomed;        /* did RPL_WELCOME (001) arrive? */
	bool refused;         /* did ERR_NEEDMOREPARAMS (461) arrive? */
	std::string username; /* as WHOIS reports it back */
	std::string realname;
	std::string umodes;   /* as RPL_UMODEIS reports it back */
	std::string raw;
};

/* Reads from `c` until `fence` appears, or the budget runs out.
 *
 * Every case below is one round trip, so waiting on a fixed sleep would cost
 * the suite roughly a second per case and still race on a loaded machine.
 * The whole exchange is pipelined into one write instead and terminated by a
 * PING whose PONG is the fence: the read returns the moment the server has
 * finished answering, which is both faster and stricter than any sleep. */
static std::string recvUntil(TestClient &c, const std::string &fence,
							 const std::string &altFence, int budgetMs = 2000)
{
	std::string acc;
	const int sliceMs = 40;
	for (int waited = 0; waited < budgetMs; waited += sliceMs)
	{
		struct timeval tv;
		tv.tv_sec = 0;
		tv.tv_usec = sliceMs * 1000;
		setsockopt(c.fd(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

		char buf[8192];
		ssize_t n = recv(c.fd(), buf, sizeof(buf) - 1, 0);
		if (n > 0)
		{
			buf[n] = '\0';
			acc += buf;
			if (acc.find(fence) != std::string::npos ||
				acc.find(altFence) != std::string::npos)
				return acc;
			waited = 0; /* progress: give it the full budget again */
		}
		else if (n == 0)
		{
			return acc; /* peer closed */
		}
	}
	return acc;
}

/* Drives one full registration and reads the result back.
 *
 * `userLine` is sent verbatim, so a case can be malformed in ways a typed
 * API could not express. PASS and NICK are always well-formed, so the only
 * variable in the experiment is the USER line. */
static Registration registerWith(int port, const std::string &nick,
								 const std::string &userLine)
{
	Registration out;
	out.welcomed = false;
	out.refused = false;

	TestClient c;
	if (!c.connect(port))
		return out;

	/* One write, in protocol order. WHOIS and MODE only answer once
	 * registered; when they do not, their fields simply stay empty and the
	 * welcomed flag carries the verdict. PING answers either way, which is
	 * what makes it usable as the fence. */
	c.sendRaw("PASS testpass\r\n"
			  "NICK " + nick + "\r\n" +
			  userLine + "\r\n"
			  "WHOIS " + nick + "\r\n"
			  "MODE " + nick + "\r\n"
			  "PING :fence\r\n");

	/* PING is the last command in the burst, so whichever way it is answered
	 * the server has finished with everything before it. A registered client
	 * gets the PONG carrying the token; one whose USER was refused gets
	 * ERR_NOTREGISTERED instead — both are equally good fences, and waiting
	 * only for the PONG would stall every refusal case for the full budget. */
	out.raw = recvUntil(c, "fence", " " + std::string(ERR_NOTREGISTERED) + " ");
	out.welcomed = out.raw.find(" " + std::string(RPL_WELCOME) + " ") !=
				   std::string::npos;
	out.refused = out.raw.find(" " + std::string(ERR_NEEDMOREPARAMS) + " ") !=
				  std::string::npos;

	/* ":server 311 <me> <nick> <user> <host> * :<realname>" */
	std::string::size_type at =
		out.raw.find(" " + std::string(RPL_WHOISUSER) + " ");
	if (at != std::string::npos)
	{
		std::string::size_type eol = out.raw.find("\r\n", at);
		std::string line = out.raw.substr(at, eol - at);
		std::string::size_type colon = line.find(" :");
		if (colon != std::string::npos)
			out.realname = line.substr(colon + 2);

		std::istringstream is(line.substr(0, colon));
		std::string tok;
		std::vector<std::string> toks;
		while (is >> tok)
			toks.push_back(tok);
		/* 311 <me> <nick> <user> <host> * */
		if (toks.size() >= 4)
			out.username = toks[3];
	}

	/* ":server 221 <me> <modes>" */
	at = out.raw.find(" " + std::string(RPL_UMODEIS) + " ");
	if (at != std::string::npos)
	{
		std::string::size_type eol = out.raw.find("\r\n", at);
		std::string line = out.raw.substr(at, eol - at);
		std::string::size_type sp = line.rfind(' ');
		if (sp != std::string::npos)
			out.umodes = line.substr(sp + 1);
	}

	c.sendCmd("QUIT");
	return out;
}

static std::string itos2(int n)
{
	std::ostringstream os;
	os << n;
	return os.str();
}

/* ══════════════════════════════════════════════════════════════════════════
 * Rule 1 — arity and the trailing form
 * ══════════════════════════════════════════════════════════════════════ */

struct FormCase
{
	const char *tail;   /* everything after "USER" (empty = bare USER) */
	bool accept;
	const char *why;
};

static void runFormCases(int port, const std::string &tag,
						 const FormCase *cases, size_t n)
{
	for (size_t i = 0; i < n; ++i)
	{
		const std::string nick = tag + itos2(static_cast<int>(i));
		const std::string line =
			cases[i].tail[0] ? "USER " + std::string(cases[i].tail) : "USER";
		Registration r = registerWith(port, nick, line);

		EXPECT_EQ(r.welcomed, cases[i].accept)
			<< (cases[i].accept ? "should have registered: "
								: "should have been refused: ")
			<< "\"" << line << "\"\n      (" << cases[i].why << ")\n"
			<< r.raw;

		/* A refusal must SAY so. Dropping the line silently would leave the
		 * client waiting forever for a welcome that is never coming. */
		if (!cases[i].accept)
			EXPECT_TRUE(r.refused)
				<< "refused without answering 461: \"" << line << "\"\n"
				<< r.raw;
	}
}

TEST_F(UserMatrixTest, ArityAndTrailingForm)
{
	static const FormCase cases[] = {
		{"", false, "bare USER — no parameters at all"},
		{"Alice", false, "one parameter"},
		{"Alice 0", false, "two"},
		{"Alice 0 *", false, "three — the realname is missing"},
		{"Alice 0 * :Alice", true, "the canonical four-parameter form"},
		{"Alice 0 * Alice", false,
		 "four parameters but no trailing — the colon is grammar, not decoration"},
		{"Alice 0 * Alice Smith", false,
		 "…and without it a multi-word realname would silently lose its tail"},
		{"Alice 0 * x :y", false,
		 "five parameters — the trailing is no longer the realname slot"},
		{"Alice 0 * :", true, "an EMPTY trailing realname is still a trailing one"},
		{"Alice 0 * : ", true, "a realname of one space likewise"},
	};
	runFormCases(serverPort, "form", cases, sizeof(cases) / sizeof(cases[0]));
}

/* ══════════════════════════════════════════════════════════════════════════
 * Rule 2 — <user> follows the RFC 2812 `user` production
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(UserMatrixTest, UsernameProduction)
{
	static const FormCase cases[] = {
		{"alice 0 * :R", true, "plain"},
		{"Ali:ce 0 * :R", true, "':' is 0x3A — inside %x21-3F, so allowed"},
		{"a!b 0 * :R", true, "'!' is 0x21 — allowed, though it also splits a prefix"},
		{"a.b-c_d 0 * :R", true, "punctuation generally"},
		{"123 0 * :R", true, "digits only"},
		{"~alice 0 * :R", true, "'~', the conventional no-ident marker"},
		{"a@b 0 * :R", false, "'@' is 0x40 — excluded, and it forks the prefix"},
		{"@lead 0 * :R", false, "…at the start"},
		{"trail@ 0 * :R", false, "…and at the end"},
		{"a@b@c 0 * :R", false, "…twice over"},
	};
	runFormCases(serverPort, "user", cases, sizeof(cases) / sizeof(cases[0]));
}

TEST_F(UserMatrixTest, UsernameIsTruncatedNotRejected)
{
	/* MAX_USERLEN is a storage bound, not a grammar rule: an over-long name
	 * is cut down, the way an over-long nick is, rather than refused. */
	const std::string longName(MAX_USERLEN + 20, 'u');
	Registration r = registerWith(serverPort, "trunc1", "USER " + longName + " 0 * :R");
	EXPECT_TRUE(r.welcomed) << r.raw;
	EXPECT_EQ(r.username.size(), static_cast<size_t>(MAX_USERLEN))
		<< "username should be truncated to MAX_USERLEN, got \"" << r.username
		<< "\"";

	/* Truncation happens BEFORE validation, so an '@' past the cut cannot
	 * reach the prefix — and cannot cause a spurious refusal either. */
	Registration r2 = registerWith(serverPort, "trunc2",
								   "USER " + std::string(MAX_USERLEN, 'v') + "@evil 0 * :R");
	EXPECT_TRUE(r2.welcomed)
		<< "an '@' beyond MAX_USERLEN is cut off, not a refusal\n" << r2.raw;
	EXPECT_EQ(r2.username.find('@'), std::string::npos)
		<< "no '@' may survive into the stored username: \"" << r2.username << "\"";
}

TEST_F(UserMatrixTest, ThePrefixStaysUnambiguous)
{
	/* The point of the '@' exclusion, stated directly: whatever a client
	 * asks for, the prefix it ends up wearing has exactly one '@'. */
	static const char *attempts[] = {
		"USER a@b 0 * :R", "USER @@@ 0 * :R", "USER x@y@z 0 * :R",
		"USER normal 0 * :R",
	};
	for (size_t i = 0; i < sizeof(attempts) / sizeof(attempts[0]); ++i)
	{
		const std::string nick = "pfx" + itos2(static_cast<int>(i));
		Registration r = registerWith(serverPort, nick, attempts[i]);
		if (!r.welcomed)
			continue; /* refused outright — also fine */

		std::string::size_type at = r.raw.find(" " + std::string(RPL_WELCOME) + " ");
		ASSERT_NE(at, std::string::npos) << r.raw;
		std::string line = r.raw.substr(at, r.raw.find("\r\n", at) - at);
		std::string::size_type sp = line.rfind(' ');
		std::string prefix = line.substr(sp + 1);

		size_t ats = 0;
		for (size_t k = 0; k < prefix.size(); ++k)
			if (prefix[k] == '@')
				++ats;
		EXPECT_EQ(ats, 1u) << "prefix \"" << prefix << "\" has " << ats
						   << " '@' — a client cannot tell where the host starts";
	}
}

/* ══════════════════════════════════════════════════════════════════════════
 * Rule 3 — <mode> is a bitmask
 * ══════════════════════════════════════════════════════════════════════ */

struct BitmaskCase
{
	const char *mode;
	const char *umodes; /* what RPL_UMODEIS must report afterwards */
	const char *why;
};

TEST_F(UserMatrixTest, ModeParameterIsABitmask)
{
	static const BitmaskCase cases[] = {
		{"0", "+", "no bits — what every real client sends"},
		{"4", "+w", "bit 2 sets w"},
		{"8", "+i", "bit 3 sets i"},
		{"12", "+iw", "both bits"},
		{"15", "+iw", "bits 0 and 1 have no meaning and are ignored"},
		{"255", "+iw", "…as are all the high bits"},
		{"1", "+", "bit 0 alone sets nothing"},
		{"2", "+", "bit 1 alone sets nothing"},
		{"3", "+", "…nor the two together"},
		{"abc", "+", "a non-numeric mode carries no bits"},
		{"*", "+", "…including the one clients send for <unused>"},
		{"-5", "+", "a negative is not a bitmask"},
		{"0x8", "+", "hex is not the RFC's numeric"},
		{"08", "+i", "a leading zero is still decimal 8"},
		{"99999999999999999999", "+", "an overflowing value carries no bits"},
		{"", "+", "an empty mode carries no bits"},
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
	{
		const std::string nick = "bm" + itos2(static_cast<int>(i));
		/* An empty <mode> cannot be written as an empty token, so that case
		 * is expressed as the parameter being absent from its slot — which
		 * makes the line three-parameter and is refused by rule 1. It is
		 * listed here for completeness of the value space and skipped. */
		if (cases[i].mode[0] == '\0')
			continue;

		Registration r = registerWith(
			serverPort, nick,
			"USER u " + std::string(cases[i].mode) + " * :R");
		ASSERT_TRUE(r.welcomed)
			<< "a <mode> value must never prevent registration: \""
			<< cases[i].mode << "\"\n" << r.raw;
		EXPECT_EQ(r.umodes, std::string(cases[i].umodes))
			<< "USER <mode>=" << cases[i].mode << " — " << cases[i].why;
	}
}

/* ══════════════════════════════════════════════════════════════════════════
 * Rule 4 — <unused> is unused, but must be there
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(UserMatrixTest, UnusedParameterIsAcceptedWhateverItSays)
{
	static const FormCase cases[] = {
		{"u 0 * :R", true, "the conventional '*'"},
		{"u 0 x :R", true, "any token at all"},
		{"u 0 0 :R", true, "a number"},
		{"u 0 :R :R2", false,
		 "a ':' OPENS THE TRAILING, so <unused> cannot start with one: this "
		 "parses as three parameters (u, 0, \"R :R2\"), not four"},
		{"u 0 servername :R", true, "the hostname some clients still send"},
		{"u 0 @ :R", true, "'@' is unconstrained here, unlike in <user>"},
	};
	runFormCases(serverPort, "unu", cases, sizeof(cases) / sizeof(cases[0]));
}

/* ══════════════════════════════════════════════════════════════════════════
 * <realname> — the trailing parameter takes almost anything
 * ══════════════════════════════════════════════════════════════════════ */

struct RealnameCase
{
	const char *trailing; /* what follows the colon */
	const char *stored;   /* what WHOIS must report back */
	const char *why;
};

TEST_F(UserMatrixTest, RealnameIsTheTrailingParameterVerbatim)
{
	static const RealnameCase cases[] = {
		{"Alice", "Alice", "one word"},
		{"Alice Smith", "Alice Smith", "a space"},
		{"Alice Smith Jr.", "Alice Smith Jr.", "several words"},
		{"Alice:Smith", "Alice:Smith", "a colon inside the trailing"},
		{"::::", "::::", "…nothing but colons"},
		{"Alice * Smith", "Alice * Smith", "an asterisk"},
		{"Alice @ home", "Alice @ home", "'@' is unconstrained in a realname"},
		{"123", "123", "digits"},
		{"!@#$%^&*()", "!@#$%^&*()", "punctuation"},
		{"  leading", "  leading", "leading spaces are part of the value"},
		{"trailing  ", "trailing  ", "…and so are trailing ones"},
		{"a\tb", "a\tb", "TAB is an ordinary octet in the trailing"},
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
	{
		const std::string nick = "rn" + itos2(static_cast<int>(i));
		Registration r = registerWith(
			serverPort, nick,
			"USER u 0 * :" + std::string(cases[i].trailing));
		ASSERT_TRUE(r.welcomed) << "\"" << cases[i].trailing << "\" ("
								<< cases[i].why << ")\n" << r.raw;
		EXPECT_EQ(r.realname, std::string(cases[i].stored))
			<< "realname was not stored verbatim — " << cases[i].why;
	}
}

TEST_F(UserMatrixTest, EmptyRealnameIsAcceptedAndStoredEmpty)
{
	Registration r = registerWith(serverPort, "rnE", "USER u 0 * :");
	EXPECT_TRUE(r.welcomed) << r.raw;
	EXPECT_EQ(r.realname, "") << "an empty trailing must stay empty";
}

/* ══════════════════════════════════════════════════════════════════════════
 * The acceptance table from signatures.md, verbatim
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(UserMatrixTest, SignaturesDocumentAcceptanceTable)
{
	/* Every ✅/❌ row of the USER table in
	 * wiki/FT_IRC_CLIENT_PROTOCOL/signatures.md, in the order it is written.
	 * The CR/LF rows are not expressible as a single line and are covered by
	 * EmbeddedControlCharactersCannotSplitTheCommand below. */
	static const FormCase cases[] = {
		{"Alice 0 * :Alice", true, "Normal"},
		{"Alice 0 * :Alice Smith", true, "Space in realname"},
		{"Alice 0 * :Alice Smith Jr.", true, "Multiple spaces/words"},
		{"Alice 0 * :", true, "Empty realname"},
		{"Alice 0 * : ", true, "Realname is one space"},
		{"Alice 0 * :a b c", true, "Realname contains spaces"},
		{"Alice 0 * :Alice:Smith", true, ": allowed in trailing"},
		{"Alice 0 * :Alice * Smith", true, "* allowed"},
		{"Alice 0 * :Alice @ home", true, "Spaces/punctuation allowed"},
		{"Alice 0 * :123", true, "Fine"},
		{"Alice 0 * :!@#$%^&*()", true, "Generally valid trailing chars"},
		{"Alice 0 * :Alice\tSmith", true, "TAB rides through as an octet"},
		{"Alice 0 * Alice", false, "Missing trailing-parameter syntax"},
		{"Alice 0 *", false, "Missing realname"},
		{"Alice 0", false, "Missing parameters"},
		{"Alice", false, "Missing parameters"},
		{"", false, "Missing parameters"},
	};
	runFormCases(serverPort, "sig", cases, sizeof(cases) / sizeof(cases[0]));
}

TEST_F(UserMatrixTest, CrAndLfBothTerminateTheMessage)
{
	/* signatures.md marks a realname carrying a bare CR or LF ❌ with the
	 * reason "CR/LF terminates IRC message". That is the whole rule: neither
	 * octet can appear INSIDE a message, so a USER line carrying one is not
	 * one malformed command but two lines — and the realname is whatever
	 * preceded the break, never the two halves glued together.
	 *
	 * The gluing is the failure this guards against. Deleting a bare CR and
	 * running the halves together fabricates a realname the client never
	 * sent: "…:real\rJOIN #x" became the realname "realJOIN #x". */
	const char *breaks[] = {"\n", "\r"};
	for (size_t i = 0; i < 2; ++i)
	{
		const std::string nick = "brk" + itos2(static_cast<int>(i));
		TestClient c;
		ASSERT_TRUE(c.connect(serverPort));
		c.sendCmd("PASS testpass");
		c.sendCmd("NICK " + nick);
		std::this_thread::sleep_for(std::chrono::milliseconds(80));

		c.sendRaw("USER u 0 * :real" + std::string(breaks[i]) +
				  "PRIVMSG " + nick + " :after\r\n");
		std::this_thread::sleep_for(std::chrono::milliseconds(250));
		c.sendCmd("WHOIS " + nick);
		std::this_thread::sleep_for(std::chrono::milliseconds(250));
		std::string got = c.recvAll(300);

		EXPECT_NE(got.find(" :real\r\n"), std::string::npos)
			<< "the realname must stop at the break, not absorb what follows"
			<< " (break = " << (i ? "CR" : "LF") << "):\n" << got;
		EXPECT_EQ(got.find("realPRIVMSG"), std::string::npos)
			<< "the two halves were glued into one realname:\n" << got;
	}
}

TEST_F(UserMatrixTest, NoControlOctetSurvivesIntoTheStoredIdentity)
{
	/* Whatever the client sends, what the server stores and later echoes
	 * back must not contain an octet that would re-frame the line it is
	 * echoed on. */
	TestClient c;
	ASSERT_TRUE(c.connect(serverPort));
	c.sendCmd("PASS testpass");
	c.sendCmd("NICK ctl1");
	std::this_thread::sleep_for(std::chrono::milliseconds(80));
	c.sendRaw("USER u\x01v 0 * :re\x02al\r\n");
	std::this_thread::sleep_for(std::chrono::milliseconds(250));
	c.sendCmd("WHOIS ctl1");
	std::this_thread::sleep_for(std::chrono::milliseconds(250));
	std::string got = c.recvAll(300);

	std::string::size_type at = got.find(" " + std::string(RPL_WHOISUSER) + " ");
	ASSERT_NE(at, std::string::npos) << got;
	std::string line = got.substr(at, got.find("\r\n", at) - at);
	EXPECT_EQ(line.find('\r'), std::string::npos)
		<< "a CR survived into a reply line: " << line;
	EXPECT_EQ(line.find('\n'), std::string::npos)
		<< "an LF survived into a reply line: " << line;

	c.sendCmd("QUIT");
}

/* ══════════════════════════════════════════════════════════════════════════
 * Density and ordering
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(UserMatrixTest, ALongRealnameIsStoredWithinTheLineLimit)
{
	/* The realname comes back inside a 311, which carries a prefix, the
	 * numeric, two nicks and a host on top of it. A realname long enough to
	 * fill an incoming 512-byte line must not produce an outgoing one that
	 * overruns it. */
	const std::string head = "USER u 0 * :";
	const std::string big(MAX_MSGLEN - 2 - head.size(), 'R');

	Registration r = registerWith(serverPort, "big1", head + big);
	ASSERT_TRUE(r.welcomed) << r.raw;

	size_t worst = 0;
	std::string::size_type start = 0;
	while (start < r.raw.size())
	{
		std::string::size_type end = r.raw.find("\r\n", start);
		if (end == std::string::npos)
			break;
		if ((end - start) + 2 > worst)
			worst = (end - start) + 2;
		start = end + 2;
	}
	EXPECT_LE(worst, static_cast<size_t>(MAX_MSGLEN))
		<< "a long realname produced an over-long reply";
}

TEST_F(UserMatrixTest, RegistrationOrderAndReregistration)
{
	/* USER before NICK must work as well as NICK before USER — registration
	 * completes on whichever arrives second. And once registered, a second
	 * USER is 462, never a silent identity change. */
	TestClient c;
	ASSERT_TRUE(c.connect(serverPort));
	c.sendCmd("PASS testpass");
	c.sendCmd("USER first 0 * :First");
	std::this_thread::sleep_for(std::chrono::milliseconds(120));
	c.sendCmd("NICK ordered");
	std::this_thread::sleep_for(std::chrono::milliseconds(250));
	std::string got = c.recvAll(300);
	EXPECT_NE(got.find(" " + std::string(RPL_WELCOME) + " "), std::string::npos)
		<< "USER before NICK must still register:\n" << got;

	c.sendCmd("USER second 0 * :Second");
	std::this_thread::sleep_for(std::chrono::milliseconds(220));
	got = c.recvAll(300);
	EXPECT_NE(got.find(" " + std::string(ERR_ALREADYREGISTRED) + " "),
			  std::string::npos)
		<< "a second USER must be 462:\n" << got;

	c.sendCmd("WHOIS ordered");
	std::this_thread::sleep_for(std::chrono::milliseconds(220));
	got = c.recvAll(300);
	EXPECT_NE(got.find(" first "), std::string::npos)
		<< "the re-registration attempt changed the stored username:\n" << got;

	c.sendCmd("QUIT");
}

TEST_F(UserMatrixTest, AMalformedUserDoesNotPoisonTheConnection)
{
	/* A refused USER must leave the connection usable: the client fixes its
	 * line and registers. A server that half-applied the bad one — storing
	 * the username but not completing — would either register the client
	 * under the wrong identity or wedge it forever. */
	TestClient c;
	ASSERT_TRUE(c.connect(serverPort));
	c.sendCmd("PASS testpass");
	c.sendCmd("NICK retry1");
	std::this_thread::sleep_for(std::chrono::milliseconds(80));

	c.sendCmd("USER bad@name 0 * :R");
	c.sendCmd("USER notrailing 0 * R");
	c.sendCmd("USER short 0");
	std::this_thread::sleep_for(std::chrono::milliseconds(250));
	std::string got = c.recvAll(300);
	EXPECT_EQ(got.find(" " + std::string(RPL_WELCOME) + " "), std::string::npos)
		<< "a malformed USER registered the client anyway:\n" << got;

	c.sendCmd("USER good 0 * :Recovered");
	std::this_thread::sleep_for(std::chrono::milliseconds(250));
	got = c.recvAll(300);
	EXPECT_NE(got.find(" " + std::string(RPL_WELCOME) + " "), std::string::npos)
		<< "the connection never recovered from a refused USER:\n" << got;

	c.sendCmd("WHOIS retry1");
	std::this_thread::sleep_for(std::chrono::milliseconds(220));
	got = c.recvAll(300);
	EXPECT_NE(got.find(" good "), std::string::npos)
		<< "the good username did not take:\n" << got;
	EXPECT_EQ(got.find("bad@name"), std::string::npos)
		<< "a refused username leaked into the registration:\n" << got;

	c.sendCmd("QUIT");
}

/* ══════════════════════════════════════════════════════════════════════════
 * The dense pass: every axis crossed with every other
 * ══════════════════════════════════════════════════════════════════════ */

/* The four parameters are independent, and the interesting failures live in
 * the crossings — a valid username with a junk bitmask, a rejected username
 * with a perfectly good realname, a bad arity that would have been fine had
 * the trailing been there. Enumerating the product is the only way to be
 * sure one axis is not quietly deciding another's outcome.
 *
 * The generated set is the full cross product of the value lists below,
 * which is small enough to run in one fixture and large enough that no
 * hand-written table would have covered it. The oracle is not another table:
 * it is the four rules, restated as predicates over the inputs, so the test
 * would still be right if the value lists changed. */

namespace
{
struct Axis
{
	const char *value;
	bool valid;
};

/* <user>: valid per the RFC 2812 `user` production? */
const Axis kUsers[] = {
	{"alice", true},   {"a:b", true},   {"~x", true},   {"9", true},
	{"a@b", false},    {"@a", false},   {"a@", false},
};

/* <mode>: which user modes the bitmask should produce. */
struct ModeAxis
{
	const char *value;
	const char *umodes;
};
const ModeAxis kModes[] = {
	{"0", "+"},   {"4", "+w"},  {"8", "+i"},   {"12", "+iw"},
	{"7", "+w"},  {"abc", "+"}, {"-1", "+"},
};

/* <unused>: never affects the outcome — unless it starts a trailing. */
const Axis kUnused[] = {
	{"*", true}, {"x", true}, {"0", true}, {"host.name", true},
};

/* <realname>: the trailing. `valid` here means "expressible as a trailing",
 * which everything below is; the interesting part is that it is stored back
 * verbatim whatever it contains. */
const char *kRealnames[] = {
	"R", "", " ", "two words", "a:b", "!@#$", "  pad  ",
};
}  // namespace

TEST_F(UserMatrixTest, DenseCrossProductOfEveryParameterAxis)
{
	const size_t nU = sizeof(kUsers) / sizeof(kUsers[0]);
	const size_t nM = sizeof(kModes) / sizeof(kModes[0]);
	const size_t nX = sizeof(kUnused) / sizeof(kUnused[0]);
	const size_t nR = sizeof(kRealnames) / sizeof(kRealnames[0]);

	size_t caseNo = 0;
	size_t accepted = 0;
	size_t refused = 0;

	for (size_t u = 0; u < nU; ++u)
		for (size_t m = 0; m < nM; ++m)
			for (size_t x = 0; x < nX; ++x)
				for (size_t r = 0; r < nR; ++r)
				{
					const std::string line = "USER " + std::string(kUsers[u].value) +
											 " " + kModes[m].value + " " +
											 kUnused[x].value + " :" + kRealnames[r];
					const std::string nick = "d" + itos2(static_cast<int>(caseNo));
					++caseNo;

					/* The oracle, from the rules rather than a table: the
					 * form is always well-shaped here (four params, trailing
					 * fourth), so acceptance turns on <user> alone. */
					const bool shouldAccept = kUsers[u].valid;

					Registration got = registerWith(serverPort, nick, line);

					ASSERT_EQ(got.welcomed, shouldAccept)
						<< "case " << caseNo << ": \"" << line << "\"\n"
						<< got.raw;

					if (!shouldAccept)
					{
						++refused;
						EXPECT_TRUE(got.refused)
							<< "case " << caseNo << " refused silently: \"" << line
							<< "\"\n" << got.raw;
						continue;
					}
					++accepted;

					/* <user> survives verbatim (nothing here exceeds
					 * MAX_USERLEN, so truncation is not in play). */
					EXPECT_EQ(got.username, std::string(kUsers[u].value))
						<< "case " << caseNo << ": <user> was not stored verbatim";

					/* <mode> produced exactly the bits the RFC assigns, and
					 * <unused> and <realname> did not disturb them. */
					EXPECT_EQ(got.umodes, std::string(kModes[m].umodes))
						<< "case " << caseNo << ": <mode>=" << kModes[m].value
						<< " with <unused>=" << kUnused[x].value
						<< " gave the wrong user modes";

					/* <realname> came back byte for byte. */
					EXPECT_EQ(got.realname, std::string(kRealnames[r]))
						<< "case " << caseNo << ": <realname> was not stored verbatim";
				}

	/* Guard the generator itself: a bug that collapsed a value list would
	 * otherwise turn this into a test that asserts almost nothing. */
	EXPECT_EQ(caseNo, nU * nM * nX * nR);
	EXPECT_GT(accepted, 0u);
	EXPECT_GT(refused, 0u);
}

TEST_F(UserMatrixTest, DenseArityCrossProduct)
{
	/* The other half of the product: hold the values good and vary the SHAPE.
	 * Every arity from zero to five parameters, each with and without the
	 * trailing colon on the last one. Exactly one combination is legal. */
	struct Shape
	{
		const char *tail;
		bool accept;
	};
	static const Shape shapes[] = {
		{"", false},                  /* 0 params */
		{"u", false},                 /* 1 */
		{"u :R", false},              /* 1 + trailing */
		{"u 0", false},               /* 2 */
		{"u 0 :R", false},            /* 2 + trailing */
		{"u 0 *", false},             /* 3 */
		{"u 0 * :R", true},           /* 3 + trailing  <- the only legal shape */
		{"u 0 * R", false},           /* 4, no trailing */
		{"u 0 * x :R", false},        /* 4 + trailing */
		{"u 0 * x y", false},         /* 5, no trailing */
		{"u 0 * x y :R", false},      /* 5 + trailing */
		{"u 0 * x y z :R", false},    /* 6 + trailing */
	};

	size_t legal = 0;
	for (size_t i = 0; i < sizeof(shapes) / sizeof(shapes[0]); ++i)
	{
		const std::string nick = "sh" + itos2(static_cast<int>(i));
		const std::string line =
			shapes[i].tail[0] ? "USER " + std::string(shapes[i].tail) : "USER";
		Registration r = registerWith(serverPort, nick, line);

		EXPECT_EQ(r.welcomed, shapes[i].accept)
			<< "\"" << line << "\"\n" << r.raw;
		if (shapes[i].accept)
			++legal;
		else
			EXPECT_TRUE(r.refused) << "refused silently: \"" << line << "\"\n"
								   << r.raw;
	}
	EXPECT_EQ(legal, 1u) << "exactly one shape in the table may be legal";
}

TEST_F(UserMatrixTest, DenseModeBitmaskSweep)
{
	/* Every value 0..31, checked against the RFC's own definition rather
	 * than a transcribed table: bit 2 is w, bit 3 is i, nothing else counts. */
	for (int bits = 0; bits < 32; ++bits)
	{
		std::string want = "+";
		if (bits & 8)
			want += "i";
		if (bits & 4)
			want += "w";

		const std::string nick = "bs" + itos2(bits);
		Registration r = registerWith(serverPort, nick,
									  "USER u " + itos2(bits) + " * :R");
		ASSERT_TRUE(r.welcomed) << "<mode>=" << bits << "\n" << r.raw;
		EXPECT_EQ(r.umodes, want)
			<< "<mode>=" << bits << " should give " << want;
	}
}
