/* ─── Unit tests: GrammarMatcher ───
**
** Compilation is tested in test_abnfcompiler.cpp. This file tests MATCHING:
* does a
** line satisfy a production, what does it capture, and -- the part that is not
** decoration -- does an adversarial line come back as a non-match instead of
** running forever.
*/

#include <gtest/gtest.h>

#include <string>

#include "grammar/AbnfCompiler.hpp"
#include "grammar/EmbeddedGrammarSource.hpp"
#include "grammar/Grammar.hpp"
#include "grammar/GrammarMatcher.hpp"
#include "grammar/MatchResult.hpp"

namespace {

/* The productions the server runs on. `middle` and `trailing` are the RFC's
** own; the command rules sit on top of them. */
const char* kIrc =
    "nospcrlfcl =  %x01-09 / %x0B-0C / %x0E-1F / %x21-39 / %x3B-FF\n"
    "middle     =  nospcrlfcl *( \":\" / nospcrlfcl )\n"
    "trailing   =  *( \":\" / \" \" / nospcrlfcl )\n"
    "SPACE      =  %x20\n"
    "letter     =  %x41-5A / %x61-7A\n"
    "digit      =  %x30-39\n"
    "special    =  %x5B-60 / %x7B-7D\n"
    "nickname   =  ( letter / special ) *8( letter / digit / special / \"-\" "
    ")\n"
    "username   =  middle\n"
    "usermode   =  middle\n"
    "unused     =  middle\n"
    "realname   =  trailing\n"
    "password   =  middle\n"
    "chanlist   =  middle\n"
    "keylist    =  middle\n"
    "target     =  middle\n"
    "text       =  trailing\n"
    "pass-cmd   =  \"PASS\" SPACE $password\n"
    "nick-cmd   =  \"NICK\" SPACE $nickname\n"
    "user-cmd   =  \"USER\" SPACE $username SPACE $usermode SPACE $unused"
    " SPACE \":\" $realname\n"
    "join-cmd   =  \"JOIN\" SPACE ( \"0\" / $chanlist [ SPACE $keylist ] )\n"
    "privmsg-cmd = \"PRIVMSG\" SPACE $target SPACE \":\" $text\n";

/* Grammar holds, AbnfCompiler builds, GrammarMatcher walks. A fixture wires
** the three together so a test can just say "does this line match?". */
class MatcherFixture : public ::testing::Test {
 protected:
  MatcherFixture() : _matcher(NULL) {}
  ~MatcherFixture() { delete _matcher; }

  bool compile(const std::string& text) {
    AbnfCompiler compiler;
    if (!compiler.compile(text, _grammar)) {
      _error = compiler.error();
      return false;
    }
    delete _matcher;
    _matcher = new GrammarMatcher(_grammar);
    return true;
  }

  bool m(const char* rule, const std::string& line) {
    return _matcher->match(_grammar.ruleIndex(rule), line, r);
  }

  bool exhausted() const { return _matcher->lastExhausted(); }

  Grammar _grammar;
  GrammarMatcher* _matcher;
  std::string _error;
  MatchResult r;
};

class GrammarTest : public MatcherFixture {
 protected:
  void SetUp() { ASSERT_TRUE(compile(kIrc)) << _error; }
};

}  // namespace

/* ── The signatures.md USER acceptance table ──
**
** wiki/FT_IRC_CLIENT_PROTOCOL/signatures.md lists twenty USER lines and marks
** each accept or reject. That table is the reason the grammar exists, so it is
** transcribed here row for row. The rows carrying CR or LF are absent on
** purpose: framing splits on LF and Client::sanitizeLine strips CR, so such a
** line can never reach a production. */

TEST_F(GrammarTest, UserTableAcceptsPlainRealname) {
  EXPECT_TRUE(m("user-cmd", "USER Alice 0 * :Alice"));
  EXPECT_EQ(r.get("realname"), "Alice");
}

TEST_F(GrammarTest, UserTableAcceptsSpaceInRealname) {
  EXPECT_TRUE(m("user-cmd", "USER Alice 0 * :Alice Smith"));
  EXPECT_EQ(r.get("realname"), "Alice Smith");
}

TEST_F(GrammarTest, UserTableAcceptsManyWordsInRealname) {
  EXPECT_TRUE(m("user-cmd", "USER Alice 0 * :Alice Smith Jr."));
  EXPECT_EQ(r.get("realname"), "Alice Smith Jr.");
}

/* Present-but-empty is not the same as absent, and the distinction has to
** survive all the way to the handler. */
TEST_F(GrammarTest, UserTableAcceptsEmptyRealname) {
  EXPECT_TRUE(m("user-cmd", "USER Alice 0 * :"));
  EXPECT_TRUE(r.has("realname"));
  EXPECT_EQ(r.get("realname"), "");
}

TEST_F(GrammarTest, UserTableAcceptsRealnameThatIsOneSpace) {
  EXPECT_TRUE(m("user-cmd", "USER Alice 0 * : "));
  EXPECT_EQ(r.get("realname"), " ");
}

TEST_F(GrammarTest, UserTableAcceptsColonInsideRealname) {
  EXPECT_TRUE(m("user-cmd", "USER Alice 0 * :Alice:Smith"));
  EXPECT_EQ(r.get("realname"), "Alice:Smith");
}

TEST_F(GrammarTest, UserTableAcceptsPunctuationInRealname) {
  EXPECT_TRUE(m("user-cmd", "USER Alice 0 * :Alice * Smith"));
  EXPECT_TRUE(m("user-cmd", "USER Alice 0 * :Alice @ home"));
  EXPECT_TRUE(m("user-cmd", "USER Alice 0 * :123"));
  EXPECT_TRUE(m("user-cmd", "USER Alice 0 * :!@#$%^&*()"));
}

TEST_F(GrammarTest, UserTableAcceptsTabInRealname) {
  /* TAB is %x09, inside nospcrlfcl, so `trailing` admits it. signatures.md
  ** flags this row as parser-dependent; this is where that is decided. */
  EXPECT_TRUE(m("user-cmd", "USER Alice 0 * :Alice\tSmith"));
  EXPECT_EQ(r.get("realname"), "Alice\tSmith");
}

/* The row this whole design exists for: without the colon it is not a
** trailing parameter, and the RFC's USER examples always show one. */
TEST_F(GrammarTest, UserTableRejectsMissingColon) {
  EXPECT_FALSE(m("user-cmd", "USER Alice 0 * Alice"));
}

TEST_F(GrammarTest, UserTableRejectsMissingParameters) {
  EXPECT_FALSE(m("user-cmd", "USER Alice 0 *"));
  EXPECT_FALSE(m("user-cmd", "USER Alice 0"));
  EXPECT_FALSE(m("user-cmd", "USER Alice"));
  EXPECT_FALSE(m("user-cmd", "USER"));
}

/* ── Captures ── */

TEST_F(GrammarTest, UserCapturesEveryField) {
  ASSERT_TRUE(m("user-cmd", "USER guest 8 * :Ronnie Reagan"));
  EXPECT_EQ(r.get("username"), "guest");
  EXPECT_EQ(r.get("usermode"), "8");
  EXPECT_EQ(r.get("unused"), "*");
  EXPECT_EQ(r.get("realname"), "Ronnie Reagan");
}

TEST_F(GrammarTest, AbsentOptionalCaptureIsReportedAbsent) {
  ASSERT_TRUE(m("join-cmd", "JOIN #foo"));
  EXPECT_TRUE(r.has("chanlist"));
  EXPECT_FALSE(r.has("keylist"));
}

TEST_F(GrammarTest, PresentOptionalCaptureIsReportedPresent) {
  ASSERT_TRUE(m("join-cmd", "JOIN #foo,#bar key1,key2"));
  EXPECT_EQ(r.get("chanlist"), "#foo,#bar");
  EXPECT_EQ(r.get("keylist"), "key1,key2");
}

/* An alternative that skips a capture must not inherit a value written by a
** branch that was tried and abandoned. */
TEST_F(GrammarTest, BacktrackingDoesNotLeaveAStaleCapture) {
  ASSERT_TRUE(m("join-cmd", "JOIN 0"));
  EXPECT_FALSE(r.has("chanlist"));
  EXPECT_FALSE(r.has("keylist"));
}

TEST_F(GrammarTest, ResultIsResetBetweenMatches) {
  ASSERT_TRUE(m("join-cmd", "JOIN #foo key"));
  ASSERT_TRUE(r.has("keylist"));
  ASSERT_TRUE(m("join-cmd", "JOIN #foo"));
  EXPECT_FALSE(r.has("keylist"));
}

/* ── Command names ──
**
** ABNF string literals are case-insensitive, so this falls out of the grammar
** rather than out of a to_upper() call on the command token. */

TEST_F(GrammarTest, CommandNameIsCaseInsensitive) {
  EXPECT_TRUE(m("user-cmd", "user Alice 0 * :A"));
  EXPECT_TRUE(m("user-cmd", "UsEr Alice 0 * :A"));
  EXPECT_TRUE(m("user-cmd", "USER Alice 0 * :A"));
}

/* ── Whole-line matching ── */

TEST_F(GrammarTest, TrailingGarbageIsNotAMatch) {
  EXPECT_FALSE(m("nick-cmd", "NICK alice extra"));
}

TEST_F(GrammarTest, LeadingSpaceIsNotAMatch) {
  /* Leading whitespace is not in the production. Any tolerance for it belongs
  ** at the framing edge, deliberately, not smuggled in here. */
  EXPECT_FALSE(m("nick-cmd", " NICK alice"));
}

TEST_F(GrammarTest, EmptyLineMatchesNothing) {
  EXPECT_FALSE(m("nick-cmd", ""));
  EXPECT_FALSE(m("user-cmd", ""));
}

/* ── nickname, straight from the RFC production ── */

TEST_F(GrammarTest, NicknameAcceptsLetterAndSpecialLeadIns) {
  EXPECT_TRUE(m("nick-cmd", "NICK alice"));
  EXPECT_TRUE(m("nick-cmd", "NICK [zbr]"));
  EXPECT_TRUE(m("nick-cmd", "NICK {zbc}"));
  EXPECT_TRUE(m("nick-cmd", "NICK z|pipe"));
  EXPECT_TRUE(m("nick-cmd", "NICK z-dash"));
}

/* D1 in wiki/RFC-CONFORMANCE.md: `special = %x5B-60` is nine characters and
** includes the backtick. The hand-written check enumerated eight and dropped
** it; a grammar transcribed from the RFC cannot make that mistake. */
TEST_F(GrammarTest, NicknameAcceptsBacktick) {
  EXPECT_TRUE(m("nick-cmd", "NICK z`tick"));
}

TEST_F(GrammarTest, NicknameRejectsLeadingDigitAndPunctuation) {
  EXPECT_FALSE(m("nick-cmd", "NICK 1abc"));
  EXPECT_FALSE(m("nick-cmd", "NICK -abc"));
  EXPECT_FALSE(m("nick-cmd", "NICK #abc"));
  EXPECT_FALSE(m("nick-cmd", "NICK a.b"));
  EXPECT_FALSE(m("nick-cmd", "NICK a,b"));
}

/* `( letter / special ) *8( ... )` is nine characters, no more. */
TEST_F(GrammarTest, NicknameIsCappedAtNine) {
  EXPECT_TRUE(m("nick-cmd", "NICK abcdefghi"));
  EXPECT_FALSE(m("nick-cmd", "NICK abcdefghij"));
}

/* ── JOIN 0 ── */

TEST_F(GrammarTest, JoinZeroIsItsOwnAlternative) {
  EXPECT_TRUE(m("join-cmd", "JOIN 0"));
}

TEST_F(GrammarTest, JoinNeedsAnArgument) {
  EXPECT_FALSE(m("join-cmd", "JOIN"));
  EXPECT_FALSE(m("join-cmd", "JOIN "));
}

/* ── Budgets ──
**
** These are the tests that matter most. The subject grades a hang as zero, and
** backtracking over a 512-octet line is exactly where a hang would come from.
** Every line here must come back with an answer. */

TEST_F(GrammarTest, LegalMaxLengthTrailingIsAccepted) {
  /* Regression: `trailing` used to recurse once per character, so recursion
  ** depth grew with the LINE LENGTH and every legal 510-octet message blew the
  ** depth cap and was rejected. Single-octet repetitions are counted in a loop
  ** now; this test fails if that ever comes undone. */
  std::string line = "PRIVMSG #chan :";
  while (line.size() < 510) line += "x";

  EXPECT_TRUE(m("privmsg-cmd", line));
  EXPECT_FALSE(exhausted()) << "a legal line must not exhaust a budget";
  EXPECT_EQ(r.get("text").size(), 510u - 15u);
}

TEST_F(GrammarTest, LegalMaxLengthTrailingWithSpacesIsAccepted) {
  std::string line = "PRIVMSG #chan :";
  while (line.size() < 510) line += "ab ";
  line.resize(510);

  EXPECT_TRUE(m("privmsg-cmd", line));
  EXPECT_FALSE(exhausted());
}

TEST_F(GrammarTest, LegalMaxLengthRealnameIsAccepted) {
  std::string line = "USER u 0 * :";
  while (line.size() < 510) line += "Real Name ";
  line.resize(510);

  EXPECT_TRUE(m("user-cmd", line));
  EXPECT_FALSE(exhausted());
}

TEST_F(GrammarTest, LongUnmatchableLineIsRejectedNotHung) {
  std::string line = "USER ";
  for (int i = 0; i < 500; ++i) line += "a";

  EXPECT_FALSE(m("user-cmd", line));
}

TEST_F(GrammarTest, MissingSeparatorInALongLineIsRejected) {
  std::string line = "PRIVMSG ";
  for (int i = 0; i < 500; ++i) line += "a";

  EXPECT_FALSE(m("privmsg-cmd", line));
}

/* A grammar built to backtrack catastrophically must still terminate: the
** budget is what turns an exponential search into a bounded non-match. */
TEST_F(GrammarTest, CatastrophicBacktrackingTerminates) {
  ASSERT_TRUE(compile("a = *( \"x\" / \"xx\" ) \"y\"\n")) << _error;

  std::string line(64, 'x'); /* no 'y' -- every split has to be tried */
  EXPECT_FALSE(m("a", line));
}

/* A repetition whose body can match nothing must not spin. */
TEST_F(GrammarTest, ZeroWidthRepetitionTerminates) {
  ASSERT_TRUE(compile("a = *( [ \"x\" ] ) \"y\"\n")) << _error;

  EXPECT_TRUE(m("a", "xxy"));
  EXPECT_TRUE(m("a", "y"));
  EXPECT_FALSE(m("a", "xxz"));
}

/* ── Misuse ── */

TEST_F(GrammarTest, UnknownRuleNeverMatches) {
  EXPECT_EQ(_grammar.ruleIndex("no-such-rule"), Grammar::kNoRule);
  EXPECT_FALSE(_matcher->match(Grammar::kNoRule, "anything", r));
}

TEST_F(GrammarTest, CaptureLookupOfAnUnknownNameIsEmptyNotACrash) {
  ASSERT_TRUE(m("nick-cmd", "NICK alice"));
  EXPECT_FALSE(r.has("no-such-capture"));
  EXPECT_EQ(r.get("no-such-capture"), "");
}

/* ── The shipped grammar ──
**
** The embedded grammar is compiled at startup. A typo in it is a server that
** does not boot, so it is worth failing here instead of there. */

namespace {

class ShippedGrammar : public MatcherFixture {
 protected:
  void SetUp() {
    EmbeddedGrammarSource source;
    std::string text;
    ASSERT_TRUE(source.read(text)) << source.origin();
    ASSERT_TRUE(compile(text)) << _error;
  }
};

}  // namespace

TEST_F(ShippedGrammar, CompilesAndDefinesEveryCommand) {
  const char* rules[] = {"cap-cmd",    "pass-cmd",    "nick-cmd",   "user-cmd",
                         "quit-cmd",   "pong-cmd",    "ping-cmd",   "join-cmd",
                         "part-cmd",   "privmsg-cmd", "notice-cmd", "kick-cmd",
                         "invite-cmd", "topic-cmd",   "mode-cmd",   "who-cmd",
                         "whois-cmd",  "userhost-cmd"};
  for (int i = 0; i < 18; ++i)
    EXPECT_GE(_grammar.ruleIndex(rules[i]), 0) << rules[i] << " is not defined";
}

TEST_F(ShippedGrammar, UserRequiresItsColon) {
  EXPECT_TRUE(m("user-cmd", "USER a 0 * :Real Name"));
  EXPECT_EQ(r.get("realname"), "Real Name");
  EXPECT_FALSE(m("user-cmd", "USER a 0 * Real"));
}

/* NICK is deliberately permissive: an over-long nick must reach cmdNick so it
** can be truncated to NICKLEN, which a non-match could never express. */
TEST_F(ShippedGrammar, NickIsPermissiveSoTheHandlerCanTruncate) {
  EXPECT_TRUE(m("nick-cmd", "NICK abcdefghij"));
  EXPECT_EQ(r.get("newnick"), "abcdefghij");
}

/* Likewise JOIN: "#ok,#bad" must join one and answer 476 for the other, so the
** list arrives whole and the handler splits it. */
TEST_F(ShippedGrammar, JoinListArrivesWholeForTheHandlerToSplit) {
  EXPECT_TRUE(m("join-cmd", "JOIN #ok,#bad key1,key2"));
  EXPECT_EQ(r.get("chanlist"), "#ok,#bad");
  EXPECT_EQ(r.get("keylist"), "key1,key2");
}

TEST_F(ShippedGrammar, JoinZeroPartsEverything) {
  EXPECT_TRUE(m("join-cmd", "JOIN 0"));
  EXPECT_FALSE(r.has("chanlist"));
}

/* `TOPIC #c` is a query and `TOPIC #c :` clears the topic. They differ only by
** whether the trailing was present, which is exactly what the old flat
** parameter vector could not record. */
TEST_F(ShippedGrammar, TopicQueryIsDistinguishableFromTopicClear) {
  ASSERT_TRUE(m("topic-cmd", "TOPIC #c"));
  EXPECT_FALSE(r.has("topictext")) << "a query must not look like a clear";

  ASSERT_TRUE(m("topic-cmd", "TOPIC #c :"));
  EXPECT_TRUE(r.has("topictext")) << "a clear must not look like a query";
  EXPECT_EQ(r.get("topictext"), "");

  ASSERT_TRUE(m("topic-cmd", "TOPIC #c :new topic"));
  EXPECT_EQ(r.get("topictext"), "new topic");
}

TEST_F(ShippedGrammar, ModeKeepsItsArgumentsForTheSignWalker) {
  ASSERT_TRUE(m("mode-cmd", "MODE #c +it-kol secret bob 50"));
  EXPECT_EQ(r.get("modetarget"), "#c");
  EXPECT_EQ(r.get("modestring"), "+it-kol");
  EXPECT_EQ(r.get("modeargs"), "secret bob 50");
}

TEST_F(ShippedGrammar, ModeQueryHasNoModeString) {
  ASSERT_TRUE(m("mode-cmd", "MODE #c"));
  EXPECT_FALSE(r.has("modestring"));
}

TEST_F(ShippedGrammar, KickTakesCommaListsAndAnOptionalReason) {
  ASSERT_TRUE(m("kick-cmd", "KICK #a,#b u1,u2 :bye"));
  EXPECT_EQ(r.get("kickchans"), "#a,#b");
  EXPECT_EQ(r.get("kickusers"), "u1,u2");
  EXPECT_EQ(r.get("kickreason"), "bye");

  ASSERT_TRUE(m("kick-cmd", "KICK #c bob"));
  EXPECT_FALSE(r.has("kickreason"));
}

TEST_F(ShippedGrammar, WhoisSkipsAnOptionalServerParameter) {
  ASSERT_TRUE(m("whois-cmd", "WHOIS bob"));
  EXPECT_EQ(r.get("whoisnick"), "bob");
  ASSERT_TRUE(m("whois-cmd", "WHOIS irc.example.org bob"));
  EXPECT_EQ(r.get("whoisnick"), "bob");
}

TEST_F(ShippedGrammar, EveryCommandRejectsAMissingRequiredParameter) {
  EXPECT_FALSE(m("pass-cmd", "PASS"));
  EXPECT_FALSE(m("nick-cmd", "NICK"));
  EXPECT_FALSE(m("user-cmd", "USER"));
  EXPECT_FALSE(m("join-cmd", "JOIN"));
  EXPECT_FALSE(m("part-cmd", "PART"));
  EXPECT_FALSE(m("privmsg-cmd", "PRIVMSG #c"));
  EXPECT_FALSE(m("kick-cmd", "KICK #c"));
  EXPECT_FALSE(m("invite-cmd", "INVITE bob"));
  EXPECT_FALSE(m("mode-cmd", "MODE"));
}

/* QUIT, WHO, CAP and PONG are legal bare. */
TEST_F(ShippedGrammar, BareFormsThatAreLegalStayLegal) {
  EXPECT_TRUE(m("quit-cmd", "QUIT"));
  EXPECT_TRUE(m("who-cmd", "WHO"));
  EXPECT_TRUE(m("cap-cmd", "CAP"));
  EXPECT_TRUE(m("pong-cmd", "PONG"));
}

/* A 510-octet line of each shape, since that is what a real client sends at the
** limit and the budget must survive all of them. */
TEST_F(ShippedGrammar, MaxLengthLinesOfEveryShapeAreAccepted) {
  struct {
    const char* rule;
    const char* head;
  } cases[] = {
      {"privmsg-cmd", "PRIVMSG #chan :"}, {"notice-cmd", "NOTICE #chan :"},
      {"user-cmd", "USER u 0 * :"},       {"topic-cmd", "TOPIC #chan :"},
      {"kick-cmd", "KICK #chan bob :"},   {"quit-cmd", "QUIT :"}};

  for (int i = 0; i < 6; ++i) {
    std::string line = cases[i].head;
    while (line.size() < 510) line += "x";
    EXPECT_TRUE(m(cases[i].rule, line)) << cases[i].rule;
    EXPECT_FALSE(exhausted()) << cases[i].rule << " exhausted a budget";
  }
}
