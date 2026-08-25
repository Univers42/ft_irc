# ────────────────────────────────────────────────────────────────
#  tests/Makefile – builds & runs the test_runner binary
#  Compiles with C++17 (required by Google Test 1.16.0)
# ────────────────────────────────────────────────────────────────

.NOTPARALLEL:

NAME		= test_runner

CXX			= c++
CXXFLAGS	= -Wall -Wextra -std=c++17 -g
LDFLAGS		= -lpthread

# ── Include paths ──────────────────────────────────────────────
INC_PROJ	= -I ../include
INC_GTEST	= -I ../vendor/googletest/googletest/include \
			  -I ../vendor/googletest/googletest
INC_PM		= -I ../vendor
INC_LIBCPP	= -I ../vendor/libcpp/include -I ../vendor/libcpp/c98/include
INCLUDES	= $(INC_PROJ) $(INC_GTEST) $(INC_PM) $(INC_LIBCPP)

# ── libcpp str/term modules used by the project sources ───────
LIBCPP		= ../vendor/libcpp
LIBCPP_SRCS	= $(LIBCPP)/src/str/format.cpp \
			  $(LIBCPP)/src/str/case.cpp \
			  $(LIBCPP)/src/str/utf8.cpp \
			  $(LIBCPP)/src/str/secure.cpp \
			  $(LIBCPP)/src/str/base64.cpp \
			  $(LIBCPP)/src/data/date.cpp \
			  $(LIBCPP)/src/util/config.cpp \
			  $(LIBCPP)/src/term/color.cpp \
			  $(LIBCPP)/src/term/style.cpp \
			  $(LIBCPP)/src/term/table.cpp \
			  $(LIBCPP)/src/term/stylesheet.cpp \
			  $(LIBCPP)/src/term/writer.cpp

# ── libcpp C++98 tier ──────────────────────────────────────────
LIBCPP98_SRCS	= $(LIBCPP)/c98/src/line_buffer.cpp \
				  $(LIBCPP)/c98/src/csv_writer.cpp \
				  $(LIBCPP)/c98/src/reactor.cpp \
				  $(LIBCPP)/c98/src/buffered_socket.cpp \
				  $(LIBCPP)/c98/src/traffic_stats.cpp

# ── Project sources (everything except main.cpp) ──────────────
PROJ_SRCS	= ../src/tiers/tier_full.cpp \
			  ../src/Server.cpp \
			  ../src/Log.cpp \
			  ../src/extras/FancyLogSink.cpp \
			  ../src/Client.cpp \
			  ../src/Channel.cpp \
			  ../src/Message.cpp \
			  ../src/grammar/GrammarNode.cpp \
			  ../src/grammar/Grammar.cpp \
			  ../src/grammar/AbnfChars.cpp \
			  ../src/grammar/AbnfLineReader.cpp \
			  ../src/grammar/GrammarBuilder.cpp \
			  ../src/grammar/GrammarValidator.cpp \
			  ../src/grammar/MatchResult.cpp \
			  ../src/grammar/interpreted/TreeMatcher.cpp \
			  ../src/grammar/compiled/Program.cpp \
			  ../src/grammar/compiled/ProgramCompiler.cpp \
			  ../src/grammar/compiled/ProgramMatcher.cpp \
			  ../src/grammar/EmbeddedGrammarSource.cpp \
			  ../src/grammar/FileGrammarSource.cpp \
			  ../src/IrcCase.cpp \
			  ../src/IrcTrace.cpp \
			  ../src/CommandRegistration.cpp \
			  ../src/CommandChannel.cpp \
			  ../src/CommandMessaging.cpp \
			  ../src/CommandOperator.cpp \
			  ../src/CommandQuery.cpp \
			  ../src/Bot.cpp \
			  ../src/bonus/FileTransferExt.cpp

# ── Test sources ──────────────────────────────────────────────
TEST_SRCS	= test_main.cpp \
			  test_ircname.cpp \
			  test_message.cpp \
			  test_grammarbuilder.cpp \
			  test_treematcher.cpp \
			  test_matcherdifferential.cpp \
			  test_client.cpp \
			  test_channel.cpp \
			  test_bot.cpp \
			  test_integration.cpp \
			  test_robustness.cpp \
			  test_security.cpp \
			  test_filetransfer.cpp \
			  test_extensions.cpp \
			  test_libcpp98.cpp \
			  test_conformance.cpp \
			  test_libcpp_str.cpp \
			  test_postman.cpp \
			  test_modes.cpp \
			  test_user.cpp

# ── Vendor sources ────────────────────────────────────────────
GTEST_SRC	= ../vendor/googletest/googletest/src/gtest-all.cc
PM_SRC		= ../vendor/PostMan.cpp

# ── Build tree ────────────────────────────────────────────────
#  Shared with the server build: everything generated goes under the repo
#  root's build/, so `make clean` up there wipes these objects too and the
#  tests/ directory stays source-only.
# Overridable; see the note in ../Makefile. No trailing comment on this
# line: make keeps the whitespace before a '#' inside the value.
BUILDDIR	?= ../build
OBJDIR		= $(BUILDDIR)/obj/tests
BINDIR		= $(BUILDDIR)/bin
BIN			= $(BINDIR)/$(NAME)

PROJ_OBJS		= $(patsubst ../src/%.cpp,$(OBJDIR)/proj_%.o,$(PROJ_SRCS))
LIBCPP_OBJS		= $(patsubst $(LIBCPP)/src/%.cpp,$(OBJDIR)/libcpp/%.o,$(LIBCPP_SRCS))
LIBCPP98_OBJS	= $(patsubst $(LIBCPP)/c98/src/%.cpp,$(OBJDIR)/libcpp98/%.o,$(LIBCPP98_SRCS))
TEST_OBJS	= $(patsubst %.cpp,$(OBJDIR)/%.o,$(TEST_SRCS))
GTEST_OBJ	= $(OBJDIR)/gtest-all.o
PM_OBJ		= $(OBJDIR)/PostMan.o

ALL_OBJS	= $(PROJ_OBJS) $(LIBCPP_OBJS) $(LIBCPP98_OBJS) $(TEST_OBJS) $(GTEST_OBJ) $(PM_OBJ)

# ══════════════════════════════════════════════════════════════
#  Targets
# ══════════════════════════════════════════════════════════════

all: $(BIN)
	@echo "\n══════ Running tests ══════\n"
	$(BIN)

$(BIN): $(ALL_OBJS)
	@mkdir -p $(BINDIR)
	$(CXX) $(CXXFLAGS) $(ALL_OBJS) -o $(BIN) $(LDFLAGS)

# Project sources
$(OBJDIR)/proj_%.o: ../src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

# libcpp str/term modules
$(OBJDIR)/libcpp/%.o: $(LIBCPP)/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

# libcpp C++98 tier
$(OBJDIR)/libcpp98/%.o: $(LIBCPP)/c98/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

# Test sources
$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

-include $(ALL_OBJS:.o=.d)

# Google Test
$(GTEST_OBJ): $(GTEST_SRC)
	@mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(INC_GTEST) -c $< -o $@

# PostMan
$(PM_OBJ): $(PM_SRC)
	@mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(INC_PM) -c $< -o $@

build: $(BIN)
	@echo "\n══════ Built $(BIN) (not run) ══════\n"

clean:
	rm -rf $(OBJDIR)

fclean: clean
	rm -f $(BIN)

re: fclean all

.PHONY: all build  clean fclean re
