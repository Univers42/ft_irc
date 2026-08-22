/* ─── Unit tests: GrammarBuilder ───
**
** These test COMPILATION only -- turning ABNF text into a Grammar, and refusing
** text that cannot honestly be compiled. Matching lines against the result is
** TreeMatcher's job and is tested separately.
*/

#include <gtest/gtest.h>

#include "grammar/GrammarBuilder.hpp"
#include "grammar/Grammar.hpp"
#include "grammar/GrammarNode.hpp"

/* The §2.3.1 productions the server actually runs on, transcribed from
** wiki/FT_IRC_CLIENT_PROTOCOL/protocol_grammar_rules.md. Kept here in the RFC's
** own layout -- including the indented, name-elided `=/` for `params` -- so the
** reader is proved against the real thing rather than a tidied-up copy. */
static const char* kRfc2812 =
    "message    =  [ \":\" prefix SPACE ] command [ params ] crlf\n"
    "prefix     =  servername / ( nickname [ [ \"!\" user ] \"@\" host ] )\n"
    "command    =  1*letter / 3digit\n"
    "params     =  *14( SPACE middle ) [ SPACE \":\" trailing ]\n"
    "           =/ 14( SPACE middle ) [ SPACE [ \":\" ] trailing ]\n"
    "nospcrlfcl =  %x01-09 / %x0B-0C / %x0E-1F / %x21-39 / %x3B-FF\n"
    "middle     =  nospcrlfcl *( \":\" / nospcrlfcl )\n"
    "trailing   =  *( \":\" / \" \" / nospcrlfcl )\n"
    "SPACE      =  %x20        ; space character\n"
    "crlf       =  %x0D %x0A   ; carriage return / linefeed\n"
    "nickname   =  ( letter / special ) *8( letter / digit / special / \"-\" "
    ")\n"
    "chanstring =  %x01-07 / %x08-09 / %x0B-0C / %x0E-1F / %x21-2B\n"
    "chanstring =/ %x2D-39 / %x3B-FF\n"
    "key        =  1*23( %x01-05 / %x07-08 / %x0C / %x0E-1F / %x21-7F )\n"
    "letter     =  %x41-5A / %x61-7A\n"
    "digit      =  %x30-39\n"
    "special    =  %x5B-60 / %x7B-7D\n"
    "servername =  hostname\n"
    "host       =  hostname\n"
    "hostname   =  shortname *( \".\" shortname )\n"
    "shortname  =  ( letter / digit ) *( letter / digit / \"-\" )\n"
    "user       =  1*( %x01-09 / %x0B-0C / %x0E-1F / %x21-3F / %x41-FF )\n";

namespace {

/* GrammarBuilder builds, Grammar holds. This pairs them so a test can say
** "compile this and tell me what came out" in one line. */
class Compiled {
 public:
  bool from(const char* text) {
    Abnf::GrammarBuilder compiler;
    const bool ok = compiler.compile(std::string(text), grammar);
    error = compiler.error();
    return ok;
  }

  int rule(const char* name) const { return grammar.ruleIndex(name); }
  const Abnf::GrammarNode& rootOf(const char* name) const {
    return grammar.node(grammar.ruleRoot(grammar.ruleIndex(name)));
  }

  Abnf::Grammar grammar;
  std::string error;
};

}  // namespace

/* ── The real grammar ── */

TEST(GrammarBuilderTest, LoadsRfc2812Section231) {
  Compiled c;
  ASSERT_TRUE(c.from(kRfc2812)) << c.error;

  const char* required[] = {"message", "prefix",   "command",   "params",
                            "middle",  "trailing", "nickname",  "chanstring",
                            "key",     "special",  "nospcrlfcl"};
  for (int i = 0; i < 11; ++i) {
    int r = c.grammar.ruleIndex(required[i]);
    ASSERT_GE(r, 0) << required[i] << " missing";
    EXPECT_GE(c.grammar.ruleRoot(r), 0) << required[i] << " has no body";
  }
}

/* The RFC elides the rule name on its incremental alternative and indents it.
** An indented line is normally a continuation, so without special handling
** `params` would be glued into one nonsensical production. */
TEST(GrammarBuilderTest, IndentedNameElidedIncrementalAlternativeBecomesAnAlt) {
  Compiled c;
  ASSERT_TRUE(c.from(kRfc2812)) << c.error;

  ASSERT_GE(c.rule("params"), 0);
  EXPECT_EQ(c.rootOf("params").kind, Abnf::GrammarNode::Alternation);
  EXPECT_EQ(c.rootOf("params").count, 2);
}

/* `chanstring =/ ...` at column 0 is the ordinary spelling and must work too.
 */
TEST(GrammarBuilderTest, ColumnZeroIncrementalAlternativeBecomesAnAlt) {
  Compiled c;
  ASSERT_TRUE(c.from(kRfc2812)) << c.error;

  ASSERT_GE(c.rule("chanstring"), 0);
  EXPECT_EQ(c.rootOf("chanstring").kind, Abnf::GrammarNode::Alternation);
}

/* ── Individual constructs ── */

TEST(GrammarBuilderTest, OctetRange) {
  Compiled c;
  ASSERT_TRUE(c.from("a = %x41-5A\n")) << c.error;
  const Abnf::GrammarNode& n = c.rootOf("a");
  EXPECT_EQ(n.kind, Abnf::GrammarNode::OctetRange);
  EXPECT_EQ(n.lo, 0x41);
  EXPECT_EQ(n.hi, 0x5A);
}

TEST(GrammarBuilderTest, SingleOctetIsADegenerateRange) {
  Compiled c;
  ASSERT_TRUE(c.from("a = %x20\n")) << c.error;
  const Abnf::GrammarNode& n = c.rootOf("a");
  EXPECT_EQ(n.kind, Abnf::GrammarNode::OctetRange);
  EXPECT_EQ(n.lo, 0x20);
  EXPECT_EQ(n.hi, 0x20);
}

TEST(GrammarBuilderTest, DottedOctetConcatenation) {
  Compiled c;
  ASSERT_TRUE(c.from("a = %x0D.0A\n")) << c.error;
  const Abnf::GrammarNode& n = c.rootOf("a");
  EXPECT_EQ(n.kind, Abnf::GrammarNode::Sequence);
  EXPECT_EQ(n.count, 2);
}

TEST(GrammarBuilderTest, Literal) {
  Compiled c;
  ASSERT_TRUE(c.from("a = \"PASS\"\n")) << c.error;
  const Abnf::GrammarNode& n = c.rootOf("a");
  ASSERT_EQ(n.kind, Abnf::GrammarNode::Literal);
  EXPECT_EQ(c.grammar.literal(n.literal), "PASS");
}

TEST(GrammarBuilderTest, Alternation) {
  Compiled c;
  ASSERT_TRUE(c.from("a = \"x\" / \"y\" / \"z\"\n")) << c.error;
  const Abnf::GrammarNode& n = c.rootOf("a");
  EXPECT_EQ(n.kind, Abnf::GrammarNode::Alternation);
  EXPECT_EQ(n.count, 3);
}

TEST(GrammarBuilderTest, Concatenation) {
  Compiled c;
  ASSERT_TRUE(c.from("a = \"x\" \"y\"\n")) << c.error;
  const Abnf::GrammarNode& n = c.rootOf("a");
  EXPECT_EQ(n.kind, Abnf::GrammarNode::Sequence);
  EXPECT_EQ(n.count, 2);
}

TEST(GrammarBuilderTest, StarRepetitionIsUnbounded) {
  Compiled c;
  ASSERT_TRUE(c.from("a = *\"x\"\n")) << c.error;
  const Abnf::GrammarNode& n = c.rootOf("a");
  ASSERT_EQ(n.kind, Abnf::GrammarNode::Repetition);
  EXPECT_EQ(n.lo, 0);
  EXPECT_EQ(n.hi, Abnf::GrammarNode::kUnbounded);
}

TEST(GrammarBuilderTest, BoundedRepetitionCarriesBothBounds) {
  Compiled c;
  ASSERT_TRUE(c.from("a = 1*23\"x\"\n")) << c.error;
  const Abnf::GrammarNode& n = c.rootOf("a");
  ASSERT_EQ(n.kind, Abnf::GrammarNode::Repetition);
  EXPECT_EQ(n.lo, 1);
  EXPECT_EQ(n.hi, 23);
}

TEST(GrammarBuilderTest, ExactRepetition) {
  Compiled c;
  ASSERT_TRUE(c.from("a = 3\"x\"\n")) << c.error;
  const Abnf::GrammarNode& n = c.rootOf("a");
  ASSERT_EQ(n.kind, Abnf::GrammarNode::Repetition);
  EXPECT_EQ(n.lo, 3);
  EXPECT_EQ(n.hi, 3);
}

/* The 14-middle cap in `params` is a *14(...) -- an upper bound with no lower
** one -- so this spelling has to survive intact or the cap is lost. */
TEST(GrammarBuilderTest, UpperBoundOnlyRepetition) {
  Compiled c;
  ASSERT_TRUE(c.from("a = *14\"x\"\n")) << c.error;
  const Abnf::GrammarNode& n = c.rootOf("a");
  ASSERT_EQ(n.kind, Abnf::GrammarNode::Repetition);
  EXPECT_EQ(n.lo, 0);
  EXPECT_EQ(n.hi, 14);
}

TEST(GrammarBuilderTest, OptionIsAZeroOrOneRepetition) {
  Compiled c;
  ASSERT_TRUE(c.from("a = [ \"x\" ]\n")) << c.error;
  const Abnf::GrammarNode& n = c.rootOf("a");
  ASSERT_EQ(n.kind, Abnf::GrammarNode::Repetition);
  EXPECT_EQ(n.lo, 0);
  EXPECT_EQ(n.hi, 1);
}

TEST(GrammarBuilderTest, CommentsAndBlankLinesAreIgnored) {
  Compiled c;
  ASSERT_TRUE(
      c.from("; leading comment\n"
             "\n"
             "a = \"x\"   ; trailing comment\n"
             "\n"))
      << c.error;
  EXPECT_GE(c.grammar.ruleIndex("a"), 0);
}

/* A ';' inside a literal is data, not the start of a comment. */
TEST(GrammarBuilderTest, SemicolonInsideALiteralIsNotAComment) {
  Compiled c;
  ASSERT_TRUE(c.from("a = \";\"\n")) << c.error;
  const Abnf::GrammarNode& n = c.rootOf("a");
  ASSERT_EQ(n.kind, Abnf::GrammarNode::Literal);
  EXPECT_EQ(c.grammar.literal(n.literal), ";");
}

TEST(GrammarBuilderTest, IndentedLineFoldsIntoThePreviousRule) {
  Compiled c;
  ASSERT_TRUE(
      c.from("a = \"x\"\n"
             "      \"y\"\n"))
      << c.error;
  const Abnf::GrammarNode& n = c.rootOf("a");
  EXPECT_EQ(n.kind, Abnf::GrammarNode::Sequence);
  EXPECT_EQ(n.count, 2);
}

/* RFC 5234 rule names are case-insensitive. */
TEST(GrammarBuilderTest, RuleNamesAreCaseInsensitive) {
  Compiled c;
  ASSERT_TRUE(c.from("SPACE = %x20\na = space\n")) << c.error;
  EXPECT_EQ(c.grammar.ruleIndex("space"), c.grammar.ruleIndex("SPACE"));
}

/* ── The one extension: $ captures ── */

TEST(GrammarBuilderTest, DollarMarksACapture) {
  Compiled c;
  ASSERT_TRUE(
      c.from("b = \"y\"\n"
             "a = \"x\" $b\n"))
      << c.error;
  ASSERT_EQ(c.grammar.captureCount(), 1u);
  EXPECT_EQ(c.grammar.captureName(0), "b");
}

TEST(GrammarBuilderTest, UncapturedReferenceRecordsNothing) {
  Compiled c;
  ASSERT_TRUE(c.from("b = \"y\"\na = b\n")) << c.error;
  EXPECT_EQ(c.grammar.captureCount(), 0u);
}

TEST(GrammarBuilderTest, RepeatedCaptureNameInternsOnce) {
  Compiled c;
  ASSERT_TRUE(
      c.from("b = \"y\"\n"
             "a = $b $b\n"))
      << c.error;
  EXPECT_EQ(c.grammar.captureCount(), 1u);
}

TEST(GrammarBuilderTest, UserCommandProductionCapturesItsFourFields) {
  std::string src(kRfc2812);
  src += "user-cmd = \"USER\" SPACE $user SPACE $mode SPACE $unused";
  src += " SPACE \":\" $realname\n";
  src += "mode     = *digit\n";
  src += "unused   = middle\n";
  src += "realname = trailing\n";

  Compiled c;
  ASSERT_TRUE(c.from(src.c_str())) << c.error;
  ASSERT_EQ(c.grammar.captureCount(), 4u);
  EXPECT_EQ(c.grammar.captureName(0), "user");
  EXPECT_EQ(c.grammar.captureName(1), "mode");
  EXPECT_EQ(c.grammar.captureName(2), "unused");
  EXPECT_EQ(c.grammar.captureName(3), "realname");
}

/* ── Refusals ──
**
** Each of these would otherwise fail late and obscurely: an undefined rule as a
** silently unmatchable line, left recursion as a hang. They must be caught when
** the grammar loads, which is startup, with a message naming the rule. */

TEST(GrammarBuilderTest, RejectsUndefinedRule) {
  Compiled c;
  EXPECT_FALSE(c.from("a = b\n"));
  EXPECT_NE(c.error.find("never defined"), std::string::npos) << c.error;
}

TEST(GrammarBuilderTest, RejectsDirectLeftRecursion) {
  Compiled c;
  EXPECT_FALSE(c.from("b = \"x\"\na = a b\n"));
  EXPECT_NE(c.error.find("left-recursive"), std::string::npos) << c.error;
}

TEST(GrammarBuilderTest, RejectsIndirectLeftRecursion) {
  Compiled c;
  EXPECT_FALSE(c.from("a = b \"x\"\nb = a\n"));
  EXPECT_NE(c.error.find("left-recursive"), std::string::npos) << c.error;
}

/* Recursion that consumes input first is fine -- `a = "x" a` terminates. */
TEST(GrammarBuilderTest, AcceptsRightRecursion) {
  Compiled c;
  EXPECT_TRUE(c.from("a = \"x\" [ a ]\n")) << c.error;
}

/* A nullable leading element still leaves the rule at its own left edge. */
TEST(GrammarBuilderTest, RejectsLeftRecursionBehindANullablePrefix) {
  Compiled c;
  EXPECT_FALSE(c.from("a = [ \"x\" ] a\n"));
  EXPECT_NE(c.error.find("left-recursive"), std::string::npos) << c.error;
}

TEST(GrammarBuilderTest, RejectsIncrementalAlternativeBeforeDefinition) {
  Compiled c;
  EXPECT_FALSE(c.from("a =/ \"x\"\n"));
  EXPECT_NE(c.error.find("before that rule"), std::string::npos) << c.error;
}

TEST(GrammarBuilderTest, RejectsDuplicateDefinition) {
  Compiled c;
  EXPECT_FALSE(c.from("a = \"x\"\na = \"y\"\n"));
  EXPECT_NE(c.error.find("defined twice"), std::string::npos) << c.error;
}

TEST(GrammarBuilderTest, RejectsUnclosedGroup) {
  Compiled c;
  EXPECT_FALSE(c.from("a = ( \"x\"\n"));
}

TEST(GrammarBuilderTest, RejectsUnclosedOption) {
  Compiled c;
  EXPECT_FALSE(c.from("a = [ \"x\"\n"));
}

TEST(GrammarBuilderTest, RejectsUnterminatedLiteral) {
  Compiled c;
  EXPECT_FALSE(c.from("a = \"x\n"));
}

TEST(GrammarBuilderTest, RejectsBackwardsOctetRange) {
  Compiled c;
  EXPECT_FALSE(c.from("a = %x5A-41\n"));
}

TEST(GrammarBuilderTest, RejectsBackwardsRepetitionRange) {
  Compiled c;
  EXPECT_FALSE(c.from("a = 5*2\"x\"\n"));
}

TEST(GrammarBuilderTest, RejectsMissingDefinedAs) {
  Compiled c;
  EXPECT_FALSE(c.from("a \"x\"\n"));
}

TEST(GrammarBuilderTest, RejectsDollarWithoutARuleName) {
  Compiled c;
  EXPECT_FALSE(c.from("a = $\"x\"\n"));
}

TEST(GrammarBuilderTest, RejectsNonHexNumVal) {
  Compiled c;
  EXPECT_FALSE(c.from("a = %d65\n"));
}

/* The error must say *where*, or a typo in a 40-line grammar is a hunt. */
TEST(GrammarBuilderTest, ErrorNamesTheOffendingLine) {
  Compiled c;
  EXPECT_FALSE(
      c.from("a = \"x\"\n"
             "b = \"y\"\n"
             "c = ( \"z\"\n"));
  EXPECT_NE(c.error.find("line 3"), std::string::npos) << c.error;
}
