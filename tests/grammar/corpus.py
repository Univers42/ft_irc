"""Per-command conformance corpus, derived from RFC 2812.

Authored from the RFC, deliberately NOT generated from the server's own
embedded ABNF. Generating from that grammar would make the suite circular:
the server parses with it, so anything it produced would be accepted by
construction and nothing would be proved. Written independently, the corpus
tests the grammar too -- check_grammar.py runs these same lines through the
embedded ABNF and reports where the two disagree.

Each case carries the production it exercises, so a failure names the clause
that broke rather than just a line that did not work.

    want    numeric that MUST come back
    forbid  numerics that MUST NOT come back
    strict  False when RFC 2812 leaves the behaviour open -- the case still
            has to keep the server alive and well formed, but its reply is
            recorded rather than judged
"""

# Numerics that mean "I could not parse that". A syntactically valid line
# must never draw one.
SYNTAX_REJECT = ("421", "461")

PREAUTH = "preauth"
REGISTERED = "registered"


class Case(object):
    def __init__(self, cmd, line, rule, note, want=None, forbid=SYNTAX_REJECT,
                 state=REGISTERED, strict=True, fresh=False, expect_close=False,
                 expect_silence=False):
        self.cmd = cmd
        self.line = line
        self.rule = rule
        self.note = note
        self.want = want
        self.forbid = tuple(forbid) if forbid else ()
        self.state = state
        self.strict = strict
        self.fresh = fresh
        self.expect_close = expect_close
        self.expect_silence = expect_silence

    def __repr__(self):
        return "<%s %r>" % (self.cmd, self.line)


def _c(*a, **k):
    return Case(*a, **k)


CASES = []


# ── CAP ──────────────────────────────────────────────────────────────────
# cap-cmd = "CAP" [ SPACE capsub [ SPACE [ ":" ] capparam ] ] *SPACE
CASES += [
    _c("CAP", "CAP LS", "cap-cmd", "subcommand only"),
    _c("CAP", "CAP LS 302", "cap-cmd", "capsub + capparam"),
    _c("CAP", "CAP LS :302", "cap-cmd", "capparam in colon form"),
    _c("CAP", "CAP", "cap-cmd", "every element optional"),
    _c("CAP", "CAP END", "cap-cmd", "END subcommand"),
    _c("CAP", "CAP LS   ", "cap-cmd", "*SPACE tolerated after the last param"),
    _c("CAP", "cap ls", "cap-cmd", "command name is case-insensitive"),
    _c("CAP", "CAP LS", "cap-cmd", "CAP is legal before registration",
       state=PREAUTH, fresh=True),
]

# ── PASS ─────────────────────────────────────────────────────────────────
# pass-cmd = "PASS" SPACE [ ":" ] password *SPACE
CASES += [
    _c("PASS", "PASS secret", "pass-cmd", "one middle param",
       state=PREAUTH, fresh=True),
    _c("PASS", "PASS :secret", "pass-cmd", "colon form of the same param",
       state=PREAUTH, fresh=True),
    _c("PASS", "PASS secret   ", "pass-cmd", "trailing *SPACE",
       state=PREAUTH, fresh=True),
    _c("PASS", "pass secret", "pass-cmd", "lowercase command name",
       state=PREAUTH, fresh=True),
    _c("PASS", "PASS", "pass-cmd", "missing the required param",
       want="461", forbid=(), state=PREAUTH, fresh=True),
    _c("PASS", "PASS a b", "pass-cmd", "extra token past the one param",
       state=PREAUTH, fresh=True, strict=False),
]

# ── NICK ─────────────────────────────────────────────────────────────────
# nick-cmd = "NICK" SPACE [ ":" ] newnick *SPACE
# nickname = ( letter / special ) *8( letter / digit / special / "-" )
CASES += [
    _c("NICK", "NICK freshnick", "nick-cmd", "plain nickname", fresh=True),
    _c("NICK", "NICK :colonnick", "nick-cmd", "colon form", fresh=True),
    _c("NICK", "NICK nick9", "nick-cmd", "digit in the body, not the lead",
       fresh=True),
    _c("NICK", "NICK a-b", "nick-cmd", "'-' is legal in the body", fresh=True),
    _c("NICK", "NICK [\\]^_`", "nick-cmd", "special = %x5B-60", fresh=True),
    _c("NICK", "NICK {|}x", "nick-cmd", "special = %x7B-7D", fresh=True),
    _c("NICK", "NICK abcdefghi", "nickname", "9 chars, the advertised NICKLEN",
       fresh=True),
    _c("NICK", "NICK", "nick-cmd", "no nickname given",
       want="431", forbid=(), fresh=True),
    _c("NICK", "NICK 1bad", "nickname", "lead must be letter or special",
       want="432", forbid=(), fresh=True),
    _c("NICK", "NICK -bad", "nickname", "'-' may not lead",
       want="432", forbid=(), fresh=True),
    _c("NICK", "NICK abcdefghijklmno", "nickname",
       "over NICKLEN: RFC says 432, this server truncates",
       fresh=True, strict=False),
    _c("NICK", "NICK freshnick   ", "nick-cmd", "trailing *SPACE", fresh=True),
]

# ── USER ─────────────────────────────────────────────────────────────────
# user-cmd = "USER" SPACE username SPACE usermode SPACE unused SPACE ":" realname
CASES += [
    _c("USER", "USER u 0 * :Real Name", "user-cmd", "all four params",
       state=PREAUTH, fresh=True),
    _c("USER", "USER u 0 * :", "user-cmd", "realname is trailing, may be empty",
       state=PREAUTH, fresh=True),
    _c("USER", "USER u 0 * :a b c d", "user-cmd", "trailing absorbs spaces",
       state=PREAUTH, fresh=True),
    _c("USER", "USER u 8 * :Real", "user-cmd", "usermode is a free middle",
       state=PREAUTH, fresh=True),
    _c("USER", "USER u 0 *", "user-cmd", "missing realname",
       want="461", forbid=(), state=PREAUTH, fresh=True),
    _c("USER", "USER u 0", "user-cmd", "missing unused + realname",
       want="461", forbid=(), state=PREAUTH, fresh=True),
    _c("USER", "USER u", "user-cmd", "one param only",
       want="461", forbid=(), state=PREAUTH, fresh=True),
    _c("USER", "USER", "user-cmd", "bare command",
       want="461", forbid=(), state=PREAUTH, fresh=True),
    _c("USER", "USER u 0 * Real", "user-cmd",
       "RFC 2812 3.1.3 makes the ':' before realname mandatory",
       want="461", forbid=(), state=PREAUTH, fresh=True),
    _c("USER", "USER u 0 * :Real", "user-cmd", "re-registration is refused",
       want="462", forbid=()),
]

# ── QUIT ─────────────────────────────────────────────────────────────────
# quit-cmd = "QUIT" [ SPACE [ ":" ] quitmsg ] *SPACE
CASES += [
    _c("QUIT", "QUIT", "quit-cmd", "message is optional",
       fresh=True, expect_close=True, forbid=("421",)),
    _c("QUIT", "QUIT :goodbye now", "quit-cmd", "colon form, spaces inside",
       fresh=True, expect_close=True, forbid=("421",)),
    _c("QUIT", "QUIT bye", "quit-cmd", "colon is optional",
       fresh=True, expect_close=True, forbid=("421",)),
    _c("QUIT", "QUIT   ", "quit-cmd", "trailing *SPACE",
       fresh=True, expect_close=True, forbid=("421",)),
]

# ── PING / PONG ──────────────────────────────────────────────────────────
# ping-cmd = "PING" [ SPACE [ ":" ] pingtoken ] *SPACE
CASES += [
    _c("PING", "PING tok", "ping-cmd", "token echoed back in a PONG"),
    _c("PING", "PING :tok", "ping-cmd", "colon form"),
    _c("PING", "PING :two words", "ping-cmd", "trailing absorbs spaces"),
    _c("PING", "PING", "ping-cmd", "RFC 2812 says 409; token is optional here",
       strict=False),
    _c("PONG", "PONG tok", "pong-cmd", "PONG draws no reply",
       expect_silence=True),
    _c("PONG", "PONG :tok", "pong-cmd", "colon form draws no reply",
       expect_silence=True),
    _c("PONG", "PONG", "pong-cmd", "bare PONG", strict=False),
]

# ── JOIN ─────────────────────────────────────────────────────────────────
# join-cmd = "JOIN" SPACE ( "0" / chanlist [ SPACE keylist ] ) *SPACE
CASES += [
    _c("JOIN", "JOIN #ja", "join-cmd", "one channel"),
    _c("JOIN", "JOIN #jb,#jc", "join-cmd", "chanlist is comma-separated"),
    _c("JOIN", "JOIN #jd key", "join-cmd", "chanlist + keylist"),
    _c("JOIN", "JOIN #je,#jf k1,k2", "join-cmd", "parallel chan/key lists"),
    _c("JOIN", "JOIN 0", "join-cmd", "the \"0\" alternation: part everything"),
    _c("JOIN", "JOIN #jg   ", "join-cmd", "trailing *SPACE"),
    _c("JOIN", "JOIN", "join-cmd", "missing the required chanlist",
       want="461", forbid=()),
    _c("JOIN", "JOIN nohash", "join-cmd", "channel must start with '#'",
       want="476", forbid=()),
]

# ── PART ─────────────────────────────────────────────────────────────────
# part-cmd = "PART" SPACE chanlist [ SPACE [ ":" ] partmsg ] *SPACE
CASES += [
    _c("PART", "PART #probe", "part-cmd", "no reason"),
    _c("PART", "PART #probe :leaving now", "part-cmd", "colon form reason"),
    _c("PART", "PART #probe bye", "part-cmd", "colon is optional"),
    _c("PART", "PART #probe,#probe2", "part-cmd", "chanlist"),
    _c("PART", "PART", "part-cmd", "missing chanlist",
       want="461", forbid=()),
    _c("PART", "PART #nosuchchannel", "part-cmd", "unknown channel",
       want="403", forbid=()),
]

# ── PRIVMSG ──────────────────────────────────────────────────────────────
# privmsg-cmd = "PRIVMSG" SPACE msgtarget SPACE [ ":" ] msgtext *SPACE
CASES += [
    _c("PRIVMSG", "PRIVMSG #probe :hello there", "privmsg-cmd", "channel target"),
    _c("PRIVMSG", "PRIVMSG #probe hello", "privmsg-cmd", "colon is optional"),
    _c("PRIVMSG", "PRIVMSG bob :hello", "privmsg-cmd", "nick target"),
    _c("PRIVMSG", "PRIVMSG bob,#probe :hi", "privmsg-cmd", "msgtarget list"),
    _c("PRIVMSG", "PRIVMSG #probe :" + "x" * 400, "privmsg-cmd",
       "long trailing stays inside 512"),
    _c("PRIVMSG", "PRIVMSG #probe ::leading colon", "privmsg-cmd",
       "':' is legal inside trailing"),
    _c("PRIVMSG", "PRIVMSG", "privmsg-cmd", "no recipient",
       want="411", forbid=()),
    _c("PRIVMSG", "PRIVMSG #probe", "privmsg-cmd", "no text",
       want="412", forbid=()),
    _c("PRIVMSG", "PRIVMSG #probe :", "privmsg-cmd", "empty trailing is no text",
       want="412", forbid=()),
    _c("PRIVMSG", "PRIVMSG nosuchnick :hi", "privmsg-cmd", "unknown target",
       want="401", forbid=()),
]

# ── NOTICE ───────────────────────────────────────────────────────────────
# notice-cmd = "NOTICE" SPACE msgtarget SPACE [ ":" ] msgtext *SPACE
# RFC 2812 s3.3.2: a NOTICE must never draw an automatic reply.
CASES += [
    _c("NOTICE", "NOTICE #probe :hello", "notice-cmd", "channel target",
       expect_silence=True),
    _c("NOTICE", "NOTICE bob :hello", "notice-cmd", "nick target",
       expect_silence=True),
    _c("NOTICE", "NOTICE bob hello", "notice-cmd", "colon is optional",
       expect_silence=True),
    _c("NOTICE", "NOTICE nosuchnick :hi", "notice-cmd",
       "no error reply, ever -- RFC 3.3.2", expect_silence=True),
    _c("NOTICE", "NOTICE", "notice-cmd", "bare NOTICE draws no reply",
       strict=False),
]

# ── KICK ─────────────────────────────────────────────────────────────────
# kick-cmd = "KICK" SPACE kickchans SPACE kickusers [ SPACE [ ":" ] kickreason ]
CASES += [
    _c("KICK", "KICK #probe bob", "kick-cmd", "no reason"),
    _c("KICK", "KICK #probe bob :out you go", "kick-cmd", "colon form reason"),
    _c("KICK", "KICK #probe bob byebye", "kick-cmd", "colon is optional"),
    _c("KICK", "KICK #probe", "kick-cmd", "missing kickusers",
       want="461", forbid=()),
    _c("KICK", "KICK", "kick-cmd", "bare command", want="461", forbid=()),
    _c("KICK", "KICK #probe nosuchuser", "kick-cmd", "target not on channel",
       want="441", forbid=()),
]

# ── INVITE ───────────────────────────────────────────────────────────────
# invite-cmd = "INVITE" SPACE invnick SPACE invchan *SPACE
CASES += [
    _c("INVITE", "INVITE carol #probe", "invite-cmd", "both params"),
    _c("INVITE", "INVITE carol #probe   ", "invite-cmd", "trailing *SPACE"),
    _c("INVITE", "INVITE carol", "invite-cmd", "missing invchan",
       want="461", forbid=()),
    _c("INVITE", "INVITE", "invite-cmd", "bare command", want="461", forbid=()),
    _c("INVITE", "INVITE nosuchnick #probe", "invite-cmd", "unknown nick",
       want="401", forbid=()),
]

# ── TOPIC ────────────────────────────────────────────────────────────────
# topic-cmd = "TOPIC" SPACE topicchan [ SPACE ":" topictext ] *SPACE
CASES += [
    _c("TOPIC", "TOPIC #probe", "topic-cmd", "query form: 331 or 332"),
    _c("TOPIC", "TOPIC #probe :a new topic", "topic-cmd", "set form"),
    _c("TOPIC", "TOPIC #probe :", "topic-cmd", "empty topictext clears"),
    _c("TOPIC", "TOPIC", "topic-cmd", "missing topicchan",
       want="461", forbid=()),
    _c("TOPIC", "TOPIC #probe bare", "topic-cmd",
       "grammar requires ':' before topictext", strict=False),
]

# ── MODE ─────────────────────────────────────────────────────────────────
# mode-cmd = "MODE" SPACE modetarget [ SPACE modestring *13( SPACE modeparam ) ]
CASES += [
    _c("MODE", "MODE #probe", "mode-cmd", "query form, modestring omitted"),
    _c("MODE", "MODE #probe +i", "mode-cmd", "flag with no param"),
    _c("MODE", "MODE #probe -i", "mode-cmd", "removing a flag"),
    _c("MODE", "MODE #probe +t", "mode-cmd", "topic-lock flag"),
    _c("MODE", "MODE #probe +k akey", "mode-cmd", "flag taking a param"),
    _c("MODE", "MODE #probe -k akey", "mode-cmd", "-k still takes its argument"),
    _c("MODE", "MODE #probe +l 10", "mode-cmd", "numeric param"),
    _c("MODE", "MODE #probe -l", "mode-cmd", "-l takes no param"),
    _c("MODE", "MODE #probe +o bob", "mode-cmd", "operator grant"),
    _c("MODE", "MODE #probe +itk akey", "mode-cmd",
       "several letters, one param between them"),
    _c("MODE", "MODE #probe +i-t", "mode-cmd", "sign flips inside modestring"),
    _c("MODE", "MODE #probe " + " ".join(["+o"] + ["bob"] * 13), "mode-cmd",
       "*13 modeparam is the grammar's ceiling", strict=False),
    _c("MODE", "MODE", "mode-cmd", "missing modetarget",
       want="461", forbid=()),
    _c("MODE", "MODE #probe +Z", "mode-cmd", "unknown mode letter",
       want="472", forbid=()),
    _c("MODE", "MODE #probe +k", "mode-cmd", "flag needing a param, none given",
       want="461", forbid=()),
    _c("MODE", "MODE probe", "mode-cmd", "user mode query on own nick"),
    _c("MODE", "MODE probe +i", "mode-cmd", "setting a user mode"),
]

# ── WHO ──────────────────────────────────────────────────────────────────
# who-cmd = "WHO" [ SPACE whomask ] *SPACE
CASES += [
    _c("WHO", "WHO", "who-cmd", "mask is optional"),
    _c("WHO", "WHO #probe", "who-cmd", "channel mask"),
    _c("WHO", "WHO bob", "who-cmd", "nick mask"),
    _c("WHO", "WHO   ", "who-cmd", "trailing *SPACE with no mask"),
    _c("WHO", "WHO nosuchthing", "who-cmd", "unmatched mask still terminates"),
]

# ── WHOIS ────────────────────────────────────────────────────────────────
# whois-cmd = "WHOIS" SPACE [ middle SPACE ] whoisnick *SPACE
CASES += [
    _c("WHOIS", "WHOIS bob", "whois-cmd", "one-param form"),
    _c("WHOIS", "WHOIS someserver bob", "whois-cmd",
       "two-param form: [ middle SPACE ] whoisnick"),
    _c("WHOIS", "WHOIS", "whois-cmd", "no nickname given",
       want="431", forbid=()),
    _c("WHOIS", "WHOIS nosuchnick", "whois-cmd", "unknown nick",
       want="401", forbid=()),
]

# ── USERHOST ─────────────────────────────────────────────────────────────
# userhost-cmd = "USERHOST" SPACE hostnick *4( SPACE hostnick ) *SPACE
CASES += [
    _c("USERHOST", "USERHOST bob", "userhost-cmd", "one nick"),
    _c("USERHOST", "USERHOST bob carol", "userhost-cmd", "two nicks"),
    _c("USERHOST", "USERHOST bob carol bob carol bob", "userhost-cmd",
       "five nicks: hostnick *4( SPACE hostnick )"),
    _c("USERHOST", "USERHOST", "userhost-cmd", "missing the required nick",
       want="461", forbid=()),
]

# ── message framing, above the per-command rules ─────────────────────────
# message = *SPACE [ ":" prefix sp ] command [ params ] *SPACE
# command = 1*letter / 3digit
CASES += [
    _c("<message>", "  WHO", "message", "leading *SPACE before the command"),
    _c("<message>", ":irrelevant.prefix WHO", "message",
       "a client-sent prefix is tolerated and ignored"),
    _c("<message>", "who", "command", "lowercase command"),
    _c("<message>", "WhO", "command", "mixed case command"),
    _c("<message>", "NOSUCHCOMMAND", "command", "unknown command",
       want="421", forbid=()),
    _c("<message>", "PRIVMSG #probe :" + "y" * 600, "message",
       "over-long line must be cut, never split into a second command",
       strict=False),
]


def by_command():
    order, groups = [], {}
    for case in CASES:
        if case.cmd not in groups:
            groups[case.cmd] = []
            order.append(case.cmd)
        groups[case.cmd].append(case)
    return [(name, groups[name]) for name in order]


if __name__ == "__main__":
    total = 0
    for name, cases in by_command():
        strict = sum(1 for c in cases if c.strict)
        print("%-12s %3d cases (%d strict)" % (name, len(cases), strict))
        total += len(cases)
    print("%-12s %3d cases across %d commands" % ("TOTAL", total, len(by_command())))
