#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "grammar/EmbeddedGrammarSource.hpp"
#include "grammar/Grammar.hpp"
#include "grammar/GrammarBuilder.hpp"
#include "grammar/MatchResult.hpp"
#include "grammar/compiled/ProgramMatcher.hpp"
#include "grammar/interpreted/TreeMatcher.hpp"

namespace {

class MatcherDifferential : public ::testing::Test {
 protected:
  void SetUp() {
    Abnf::EmbeddedGrammarSource source;
    std::string text;
    ASSERT_TRUE(source.read(text));

    Abnf::GrammarBuilder builder;
    ASSERT_TRUE(builder.compile(text, grammar)) << builder.error();

    tree = new Abnf::Interpreted::TreeMatcher(grammar);
    pike = new Abnf::Compiled::ProgramMatcher(grammar);
    ASSERT_TRUE(pike->compileAll()) << pike->error();
  }

  void TearDown() {
    delete tree;
    delete pike;
  }

  void agree(const char* rule, const std::string& line) {
    const int id = grammar.ruleIndex(rule);
    ASSERT_GE(id, 0) << rule;

    Abnf::MatchResult a;
    Abnf::MatchResult b;
    const bool ra = tree->match(id, line, a);
    const bool rb = pike->match(id, line, b);

    ASSERT_EQ(ra, rb) << rule << " <<" << line << ">>";
    if (!ra) return;

    for (std::size_t i = 0; i < grammar.captureCount(); ++i) {
      const std::string& name = grammar.captureName(static_cast<int>(i));
      ASSERT_EQ(a.count(name), b.count(name))
          << rule << " <<" << line << ">> capture " << name;
      for (std::size_t k = 0; k < a.count(name); ++k)
        ASSERT_EQ(a.at(name, k), b.at(name, k))
            << rule << " <<" << line << ">> capture " << name << "[" << k << "]";
    }
  }

  Abnf::Grammar grammar;
  Abnf::Interpreted::TreeMatcher* tree;
  Abnf::Compiled::ProgramMatcher* pike;
};

const char* kCommands[] = {"PRIVMSG", "NOTICE", "JOIN",  "PART", "MODE",
                           "KICK",    "INVITE", "TOPIC", "NICK", "USER",
                           "PASS",    "QUIT",   "PING",  "PONG", "WHO",
                           "WHOIS",   "CAP",    "USERHOST", "xyz", "001"};

const char* kArguments[] = {"",
                            "#chan",
                            "#a,#b",
                            "nick",
                            "*",
                            "0",
                            "+it",
                            "+it-kol a b c",
                            ":trailing text",
                            "#c :hello",
                            "a b c",
                            "a b :c d",
                            "#c bob :reason",
                            "x 0 * :Real Name",
                            ":",
                            "a  b",
                            " trail "};

}  // namespace

TEST_F(MatcherDifferential, BothStrategiesCompileTheWholeGrammar) {
  EXPECT_FALSE(grammar.isEmpty());
  EXPECT_GT(grammar.ruleCount(), 0u);
}

TEST_F(MatcherDifferential, StrategiesReportDifferentNames) {
  EXPECT_STRNE(tree->strategy(), pike->strategy());
}

TEST_F(MatcherDifferential, AgreeOnTheGenericMessageProduction) {
  for (int c = 0; c < 20; ++c)
    for (int a = 0; a < 17; ++a) {
      std::string line = kCommands[c];
      if (kArguments[a][0] != '\0') {
        line += " ";
        line += kArguments[a];
      }
      agree("message", line);
    }
}

TEST_F(MatcherDifferential, AgreeOnEveryCommandProduction) {
  const char* rules[] = {"pass-cmd",    "nick-cmd",   "user-cmd",
                         "join-cmd",    "part-cmd",   "privmsg-cmd",
                         "notice-cmd",  "kick-cmd",   "invite-cmd",
                         "topic-cmd",   "mode-cmd",   "who-cmd",
                         "whois-cmd",   "userhost-cmd", "quit-cmd",
                         "ping-cmd",    "pong-cmd",   "cap-cmd"};

  for (int r = 0; r < 18; ++r)
    for (int c = 0; c < 20; ++c)
      for (int a = 0; a < 17; ++a) {
        std::string line = kCommands[c];
        if (kArguments[a][0] != '\0') {
          line += " ";
          line += kArguments[a];
        }
        agree(rules[r], line);
      }
}

TEST_F(MatcherDifferential, AgreeAcrossTheParameterCap) {
  for (int n = 0; n <= 22; ++n) {
    std::string line = "PRIVMSG";
    for (int i = 0; i < n; ++i) {
      line += " p";
      line += static_cast<char>('0' + i % 10);
    }
    agree("message", line);
  }
}

TEST_F(MatcherDifferential, AgreeOnMaxLengthLines) {
  for (int n = 1; n <= 520; n += 13) {
    std::string line = "PRIVMSG #c :";
    line.append(static_cast<std::size_t>(n), 'x');
    agree("message", line);
  }
}

TEST_F(MatcherDifferential, AgreeOnCaptureListsNotJustPresence) {
  const int id = grammar.ruleIndex("message");
  Abnf::MatchResult a;
  Abnf::MatchResult b;

  ASSERT_TRUE(tree->match(id, "MODE #c +it-kol x y z", a));
  ASSERT_TRUE(pike->match(id, "MODE #c +it-kol x y z", b));

  ASSERT_EQ(a.count("param"), 5u);
  ASSERT_EQ(b.count("param"), 5u);
  for (std::size_t k = 0; k < 5; ++k) EXPECT_EQ(a.at("param", k), b.at("param", k));
}

TEST_F(MatcherDifferential, CompiledStrategyDecidesWhereInterpreterGivesUp) {
  Abnf::Grammar hostile;
  Abnf::GrammarBuilder builder;
  ASSERT_TRUE(builder.compile("a = *( \"x\" / \"xx\" ) \"y\"\n", hostile))
      << builder.error();

  Abnf::Interpreted::TreeMatcher backtracking(hostile);
  Abnf::Compiled::ProgramMatcher linear(hostile);
  ASSERT_TRUE(linear.compileAll()) << linear.error();

  const int id = hostile.ruleIndex("a");
  const std::string line(400, 'x');

  Abnf::MatchResult a;
  Abnf::MatchResult b;

  EXPECT_FALSE(backtracking.match(id, line, a));
  EXPECT_TRUE(backtracking.lastExhausted())
      << "the interpreter is expected to hit its budget on this shape";

  EXPECT_FALSE(linear.match(id, line, b));
  EXPECT_FALSE(linear.lastExhausted())
      << "the compiled strategy has no budget to exhaust";
}

TEST_F(MatcherDifferential, CompiledStrategyRefusesRecursiveRules) {
  Abnf::Grammar recursive;
  Abnf::GrammarBuilder builder;
  ASSERT_TRUE(builder.compile("a = \"x\" [ a ]\n", recursive))
      << builder.error();

  Abnf::Compiled::ProgramMatcher matcher(recursive);
  EXPECT_FALSE(matcher.compileAll());
  EXPECT_NE(matcher.error().find("recursive"), std::string::npos)
      << matcher.error();
}
