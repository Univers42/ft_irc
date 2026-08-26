# **************************************************************************** #
#                                                                              #
#                                                         :::      ::::::::    #
#    Makefile                                           :+:      :+:    :+:    #
#                                                     +:+ +:+         +:+      #
#    By: dlesieur <dlesieur@student.42.fr>          +#+  +:+       +#+         #
#                                                 +#+#+#+#+#+   +#+            #
#    Created: 2026/08/24 06:02:55 by dlesieur          #+#    #+#              #
#    Updated: 2026/08/24 06:02:58 by dlesieur         ###   ########.fr        #
#                                                                              #
# **************************************************************************** #


NAME		= ircserv

CXX			= c++
CXXFLAGS	= -Wall -Wextra -Werror -std=c++98

.NOTPARALLEL:

.DEFAULT_GOAL := help

SRCDIR		= src
TESTS_DIR	= tests

COLOR ?= auto
ifeq ($(COLOR),auto)
    ifeq ($(origin MAKE_TERMOUT),undefined)
        COLOR := 0
    else
        COLOR := 1
    endif
endif
ifdef NO_COLOR
COLOR := 0
endif

ifeq ($(COLOR),1)
C_RST	:= \033[0m
C_DIM	:= \033[2m
C_BLD	:= \033[1m
C_GRN	:= \033[32m
C_YEL	:= \033[33m
C_BLU	:= \033[34m
C_MAG	:= \033[35m
C_CYA	:= \033[36m
S_OK	:= ✔
S_BAR	:= ──
S_ARR	:= →
S_DOT	:= ·
else
C_RST	:=
C_DIM	:=
C_BLD	:=
C_GRN	:=
C_YEL	:=
C_BLU	:=
C_MAG	:=
C_CYA	:=
S_OK	:= [ok]
S_BAR	:= --
S_ARR	:= ->
S_DOT	:= -
endif

MAKE_SHORTFLAGS := $(firstword $(MAKEFLAGS))
ifeq (,$(findstring -,$(MAKE_SHORTFLAGS)))
    ifneq (,$(findstring s,$(MAKE_SHORTFLAGS)))
        QUIET := 1
    endif
endif

ifeq ($(V),1)
AT		:=
PR_TAG	:= :
else
AT		:= @
PR_TAG	:= printf
endif
ifeq ($(QUIET),1)
AT		:= @
PR_TAG	:= :
PR_MSG	:= :
else
PR_MSG	:= printf
endif

tag = @$(PR_TAG) '%b\n' '  $(1)$(2)$(C_RST)  $(3)'
act = @$(PR_MSG) '%b\n' '  $(1)$(2)$(C_RST)  $(3)'

HINT = $(C_DIM)   make help  $(S_DOT)  targets, tiers and overridable flags$(C_RST)

TIER		?= full
# Overridable so a gate that runs `make re` can own a PRIVATE tree.
# scripts/run_tests.py hands build-norm and audit their own BUILDDIR;
# without that they fclean the tree every other suite is running against.
BUILDDIR	?= build
BINDIR		= $(BUILDDIR)/bin
OBJROOT		= $(BUILDDIR)/obj
OBJDIR		= $(OBJROOT)/$(TIER)

BIN			= $(BINDIR)/$(NAME)

LINKSTAMP	= $(OBJDIR)/.relinked

LIBCPP		= vendor/libcpp
INCLUDES	= -I include -I $(LIBCPP)/include -I $(LIBCPP)/c98/include

CORE_NAMES	= main \
			  tiers/tier_$(TIER) \
			  Server \
			  Log \
			  Client \
			  Channel \
			  Message \
			  grammar/GrammarNode \
			  grammar/Grammar \
			  grammar/AbnfChars \
			  grammar/AbnfLineReader \
			  grammar/GrammarBuilder \
			  grammar/GrammarValidator \
			  grammar/MatchResult \
			  grammar/interpreted/TreeMatcher \
			  grammar/compiled/Program \
			  grammar/compiled/ProgramCompiler \
			  grammar/compiled/ProgramMatcher \
			  grammar/EmbeddedGrammarSource \
			  grammar/FileGrammarSource \
			  IrcCase \
			  IrcTrace \
			  CommandRegistration \
			  CommandChannel \
			  CommandMessaging \
			  CommandOperator \
			  CommandQuery

BONUS_NAMES	= Bot \
			  bonus/FileTransferExt

# The resident bot colony. Bonus-tier, because it is an extension the server
# can be built without -- the mandatory tier must stay exactly the subject's
# scope. Nothing here opens a descriptor, forks, or touches the event loop:
# the bots observe through IServerExtension callbacks the server already
# dispatches, so the one-poll rule is unaffected.
BOT_NAMES	= bots/Emotion \
			  bots/Lexicon \
			  bots/Brain \
			  bots/ABot \
			  bots/Personalities \
			  bots/BotColony

EXTRA_NAMES	= extras/FancyLogSink

SRC_NAMES	= $(CORE_NAMES)
ifneq ($(TIER),mandatory)
SRC_NAMES	+= $(BONUS_NAMES) $(BOT_NAMES)
endif
ifeq ($(TIER),full)
SRC_NAMES	+= $(EXTRA_NAMES)
endif

SRCS		= $(addprefix $(SRCDIR)/,$(addsuffix .cpp,$(SRC_NAMES)))

LIBCPP_CORE_NAMES	= str/format str/case str/utf8 str/secure str/base64 \
					  data/date
LIBCPP_FULL_NAMES	= util/config term/color term/style term/table \
					  term/stylesheet term/writer

LIBCPP_NAMES	= $(LIBCPP_CORE_NAMES)
ifeq ($(TIER),full)
LIBCPP_NAMES	+= $(LIBCPP_FULL_NAMES)
endif

LIBCPP_SRCS		= $(addprefix $(LIBCPP)/src/,$(addsuffix .cpp,$(LIBCPP_NAMES)))

LIBCPP98_NAMES	= line_buffer csv_writer reactor buffered_socket \
				  traffic_stats
LIBCPP98_SRCS	= $(addprefix $(LIBCPP)/c98/src/,$(addsuffix .cpp,$(LIBCPP98_NAMES)))

OBJS			= $(SRCS:$(SRCDIR)/%.cpp=$(OBJDIR)/%.o)
LIBCPP_OBJS		= $(LIBCPP_SRCS:$(LIBCPP)/src/%.cpp=$(OBJDIR)/libcpp/%.o)
LIBCPP98_OBJS	= $(LIBCPP98_SRCS:$(LIBCPP)/c98/src/%.cpp=$(OBJDIR)/libcpp98/%.o)

all:
	@$(PR_MSG) '%b\n' '' '$(C_DIM)$(S_BAR)$(C_RST) $(C_BLD)ft_irc$(C_RST) $(C_DIM)$(S_DOT)$(C_RST) $(C_MAG)full tier$(C_RST) $(C_DIM)$(S_DOT) $(CXX) $(CXXFLAGS)$(C_RST)'
	@$(MAKE) --no-print-directory TIER=full build

bonus:
	@$(PR_MSG) '%b\n' '' '$(C_DIM)$(S_BAR)$(C_RST) $(C_BLD)ft_irc$(C_RST) $(C_DIM)$(S_DOT)$(C_RST) $(C_MAG)bonus tier$(C_RST) $(C_DIM)$(S_DOT) $(CXX) $(CXXFLAGS)$(C_RST)'
	@$(MAKE) --no-print-directory TIER=bonus build

mandatory:
	@$(PR_MSG) '%b\n' '' '$(C_DIM)$(S_BAR)$(C_RST) $(C_BLD)ft_irc$(C_RST) $(C_DIM)$(S_DOT)$(C_RST) $(C_MAG)mandatory tier$(C_RST) $(C_DIM)$(S_DOT) $(CXX) $(CXXFLAGS)$(C_RST)'
	@$(MAKE) --no-print-directory TIER=mandatory build

verify-tiers:
	@$(MAKE) --no-print-directory mandatory
	@$(MAKE) --no-print-directory bonus
	@$(MAKE) --no-print-directory all
	@$(PR_MSG) '%b\n' '$(C_GRN)$(S_OK)$(C_RST)  all three tiers built sequentially $(C_DIM)$(S_DOT)$(C_RST) -Werror clean' ''

build: $(BIN)
	@if [ -f $(LINKSTAMP) ]; then \
		rm -f $(LINKSTAMP); \
		$(PR_MSG) '%b\n' '' '$(C_GRN)$(S_OK)$(C_RST)  $(C_BLD)$(NAME)$(C_RST) $(C_DIM)$(S_DOT)$(C_RST) $(TIER) tier built $(C_DIM)$(S_ARR)$(C_RST) ./$(BIN) <port> <password>' '$(HINT)' ''; \
	else \
		$(PR_MSG) '%b\n' '' '$(C_GRN)$(S_OK)$(C_RST)  $(C_BLD)$(NAME)$(C_RST) is up to date $(C_DIM)$(S_DOT)$(C_RST) $(TIER) tier' '$(HINT)' ''; \
	fi

$(BIN): $(OBJROOT)/.tier_$(TIER) $(LIBCPP_OBJS) $(LIBCPP98_OBJS) $(OBJS)
	$(call tag,$(C_GRN),LINK  ,$(C_BLD)$(BIN)$(C_RST))
	@mkdir -p $(BINDIR)
	$(AT)$(CXX) $(CXXFLAGS) $(LIBCPP_OBJS) $(LIBCPP98_OBJS) $(OBJS) -o $(BIN)
	@mkdir -p $(OBJDIR) && touch $(LINKSTAMP)

$(OBJROOT)/.tier_$(TIER):
	@mkdir -p $(OBJROOT)
	@rm -f $(OBJROOT)/.tier_*
	@touch $@

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	@mkdir -p $(dir $@)
	$(call tag,$(C_CYA),CXX   ,$<)
	$(AT)$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(OBJDIR)/libcpp/%.o: $(LIBCPP)/src/%.cpp
	@mkdir -p $(dir $@)
	$(call tag,$(C_BLU),LIBCPP,$(C_DIM)$<$(C_RST))
	$(AT)$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(OBJDIR)/libcpp98/%.o: $(LIBCPP)/c98/src/%.cpp
	@mkdir -p $(dir $@)
	$(call tag,$(C_BLU),LIBCPP,$(C_DIM)$<$(C_RST))
	$(AT)$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

-include $(OBJS:.o=.d) $(LIBCPP_OBJS:.o=.d) $(LIBCPP98_OBJS:.o=.d)

clean:
	$(call act,$(C_YEL),CLEAN ,$(OBJROOT)/ $(C_DIM)(objects + deps, every tier)$(C_RST))
	@rm -rf $(OBJROOT)

fclean: clean
	$(call act,$(C_YEL),CLEAN ,$(BUILDDIR)/ $(C_DIM)(+ a legacy ./$(NAME) symlink)$(C_RST))
	@rm -f $(NAME)
	@rm -rf $(BUILDDIR)

re: fclean all

# ════════════════════════════════════════════════════════════════════════
#  TEST ORCHESTRATION
#
#  Every suite in the repo is reachable from here, so nobody has to remember
#  which script lives where or in what order they may run. Three layers:
#
#    STATIC   no server, no network -- norm, evloop, audit, headers, ws
#    LIVE     drives a real ircserv over TCP -- unit, shell, grammar
#    HEAVY    slow or environment-dependent -- sim, mem
#
#  Aggregates: `make check` (the everyday gate) and `make test-all`
#  (everything). Both run EVERY phase even after one fails, print a single
#  summary, and exit non-zero if any phase failed -- stopping at the first
#  failure hides the other three.
#
#  `make test-list` prints the catalogue.
# ════════════════════════════════════════════════════════════════════════

NORM_SCRIPT		= vendor/scripts/norminette.sh
EVLOOP_SCRIPT	= scripts/check_event_loop.py
AUDIT_SCRIPT	= scripts/audit.sh
HEADERS_SCRIPT	= scripts/check_tracked_headers.py
WS_SCRIPT		= scripts/normalize.sh
MEM_SCRIPT		= scripts/memcheck.sh
SIM_SCRIPT		= scripts/simulation.sh
SIM_DOWN_SCRIPT	= scripts/shutdown_simulation.sh
SHELL_SUITE		= run_all.sh

# Ports. Every suite gets its own so two of them can never collide, and each
# is overridable so a second run (or a busy machine) can be moved out of the
# way: make check IRC_PORT=7001
IRC_PORT		?= 6667
STARTUP_PORT	?= 6668
GRAMMAR_PORT	?= 7500
FUZZ_PORT		?= 7600
SIM_PORT		?= 6700
MEM_PORT		?= 6900

FUZZ_CASES		?= 600
FUZZ_MODE_CASES	?= 150
SIM_USERS		?= 6
MEM_TIER		?= full

# ── the hand-driven sandbox (`make man_sim`) ────────────────────────────
# Deliberately NOT the same knobs as the automated simulation. That one is
# sized for assertions -- six personas on a private port, torn down the
# moment its probes finish. This one is sized for a person: a bigger cast, a
# lot more channels, the conventional IRC port, and it stays up until you
# say otherwise.
MAN_SIM_PORT	?= 6667
MAN_SIM_PASS	?= pass
MAN_SIM_ROSTER	?= scripts/sim/personas_manual.conf
MAN_SIM_USERS	?= 8
# The autonomous bots are COMPILED INTO the server (src/bots/), so there is
# nothing to launch and nothing to switch off here -- they are residents of
# any bonus/full-tier ircserv. `make man_sim` just gives them a populated
# room to live in. See include/bots/ABot.hpp for the design.
# Its OWN state directory, and this is load-bearing rather than tidiness.
# simulation.sh keeps one simulation per SIM_DIR and refuses to start a second
# into a directory that already holds a sim.env. Both kinds defaulting to
# .sim/ meant `make matrix` died on contact with a running sandbox -- not on
# the port (they were always on different ones), on the state directory.
MAN_SIM_DIR		?= .sim-manual
# The OLD driver: a round-robin loop that picks a random channel and sends a
# canned line. Superseded by the in-server bot colony; kept because
# scripts/sim/driver.sh is still what `--chatter` runs, and nothing should
# silently change under anyone using it.
MAN_SIM_CHATTER	?= 0

# Extra arguments forwarded to a suite, e.g.
#   make test-shell SHELL_ARGS="--only 05"
#   make test-unit  UNIT_ARGS="--gtest_filter=Channel*"
SHELL_ARGS		?=
UNIT_ARGS		?=

TEST_ENV = IRC_PORT=$(IRC_PORT) STARTUP_PORT=$(STARTUP_PORT) \
		   GRAMMAR_PORT=$(GRAMMAR_PORT) FUZZ_PORT=$(FUZZ_PORT) \
		   FUZZ_CASES=$(FUZZ_CASES)

# Phase lists. `check` is what you run before pushing; `test-all` adds the
# two slow ones. Order is deliberate: cheap static gates first, so a typo is
# reported in seconds rather than after a ten-minute shell suite.
CHECK_PHASES	= headers whitespace norm evloop audit test-unit test-shell test-grammar
ALL_PHASES		= $(CHECK_PHASES) test-sim test-mem
QUICK_PHASES	= headers whitespace evloop test-unit

# run_phases <label> <target-list>
#   Runs each target in turn, records pass/fail, keeps going after a failure,
#   then prints one summary. Exits 1 if anything failed.
#   MAKEFLAGS is cleared per phase: some suites shell out to `make re`, and an
#   inherited jobserver fd from this invocation makes that warn (or worse).
define run_phases
	@rc=0; log=$$(mktemp); \
	start=$$(date +%s); \
	for phase in $(2); do \
		printf '\n%b\n' '$(C_DIM)$(S_BAR)$(S_BAR)$(S_BAR)$(C_RST) $(C_BLD)$(1)$(C_RST) $(C_DIM)$(S_DOT)$(C_RST) $(C_MAG)'"$$phase"'$(C_RST)'; \
		t0=$$(date +%s); \
		if MAKEFLAGS= $(MAKE) --no-print-directory $$phase; then \
			printf '%s PASS %s\n' "$$phase" "$$(( $$(date +%s) - t0 ))" >> "$$log"; \
		else \
			printf '%s FAIL %s\n' "$$phase" "$$(( $$(date +%s) - t0 ))" >> "$$log"; \
			rc=1; \
		fi; \
	done; \
	printf '\n%b\n' '$(C_BLD)  $(1) summary$(C_RST) $(C_DIM)($(S_DOT) '"$$(( $$(date +%s) - start ))"'s total)$(C_RST)'; \
	printf '%b\n' '$(C_DIM)  ----------------------------------------$(C_RST)'; \
	while read -r name status secs; do \
		if [ "$$status" = PASS ]; then \
			printf '    %-16s $(C_GRN)%-6s$(C_RST) $(C_DIM)%ss$(C_RST)\n' "$$name" "$$status" "$$secs"; \
		else \
			printf '    %-16s $(C_YEL)%-6s$(C_RST) $(C_DIM)%ss$(C_RST)\n' "$$name" "$$status" "$$secs"; \
		fi; \
	done < "$$log"; \
	rm -f "$$log"; \
	printf '\n'; \
	if [ $$rc -eq 0 ]; then \
		printf '%b\n\n' '  $(C_GRN)$(S_OK)$(C_RST)  $(1): every phase passed'; \
	else \
		printf '%b\n\n' '  $(C_YEL)!!$(C_RST)  $(1): one or more phases FAILED $(C_DIM)(scroll up for the detail)$(C_RST)'; \
	fi; \
	exit $$rc
endef

# ── aggregates ─────────────────────────────────────────────────────────
# `matrix` is the one to reach for: same suites, run concurrently behind a
# live table. `check` is the serial equivalent -- slower, but it interleaves
# nothing, so it is the one to use when you want to READ the output of a
# single failing suite rather than a summary of all of them.
RUNNER_SCRIPT	= scripts/run_tests.py
RUNNER_ARGS		?=

matrix:
	@python3 $(RUNNER_SCRIPT) $(RUNNER_ARGS)

matrix-quick:
	@python3 $(RUNNER_SCRIPT) --quick $(RUNNER_ARGS)

matrix-list:
	@python3 $(RUNNER_SCRIPT) --list

check:
	$(call run_phases,check,$(CHECK_PHASES))

test-all:
	$(call run_phases,test-all,$(ALL_PHASES))

test-quick:
	$(call run_phases,test-quick,$(QUICK_PHASES))

# ── LIVE: in-process unit tests ────────────────────────────────────────
# `test` is kept as the historical name for the Google Test suite.
test: test-unit

test-unit:
	$(call act,$(C_MAG),UNIT  ,Google Test suite $(C_DIM)(C++17, in-process, via tests/Makefile)$(C_RST))
	@MAKEFLAGS= $(MAKE) --no-print-directory -C tests build
	@$(BINDIR)/test_runner $(UNIT_ARGS)

# ── LIVE: black-box shell suite ────────────────────────────────────────
# Starts a real server and drives it over TCP the way a client would.
# 12_build_norm.sh inside it shells out to `make re`, which is why this is
# the slowest of the three live suites.
test-shell: $(BIN)
	$(call act,$(C_MAG),SHELL ,black-box suite $(C_DIM)(tests/$(SHELL_SUITE), live server on $(IRC_PORT))$(C_RST))
	@cd $(TESTS_DIR) && $(TEST_ENV) MAKEFLAGS= bash ./$(SHELL_SUITE) $(SHELL_ARGS)

# ── LIVE: RFC 2812 grammar conformance + fuzz ──────────────────────────
# Each half starts its own short-lived server on its own port, so this needs
# nothing running and cannot disturb anything that is.
test-grammar: $(BIN)
	$(call act,$(C_MAG),GRAMMR,RFC 2812 conformance $(C_DIM)(18 productions)$(C_RST))
	@python3 $(TESTS_DIR)/grammar/conformance.py --binary $(BIN) --port $(GRAMMAR_PORT)
	$(call act,$(C_MAG),FUZZ  ,structure-aware fuzz $(C_DIM)($(FUZZ_CASES) cases$(if $(FUZZ_SEED), seed $(FUZZ_SEED)))$(C_RST))
	@python3 $(TESTS_DIR)/grammar/fuzz.py --binary $(BIN) --port $(FUZZ_PORT) \
		--cases $(FUZZ_CASES) $(if $(FUZZ_SEED),--seed $(FUZZ_SEED))

# ── HEAVY: populated simulation + its conformance probes ───────────────
# The probes need a live simulation (simulation.sh's own require_running), so
# this brings one up, runs all three, and tears it down whatever happens.
test-sim: $(BIN)
	$(call act,$(C_MAG),SIM   ,populated simulation $(C_DIM)($(SIM_USERS) users on $(SIM_PORT)) + conformance probes$(C_RST))
	@rc=0; \
	bash $(SIM_SCRIPT) --port $(SIM_PORT) --users $(SIM_USERS) --no-scenario >/dev/null || \
		{ printf '  simulation failed to start\n' >&2; exit 1; }; \
	trap 'bash $(SIM_DOWN_SCRIPT) >/dev/null 2>&1 || true' EXIT INT TERM; \
	bash $(SIM_SCRIPT) --verify-names   || rc=1; \
	bash $(SIM_SCRIPT) --verify-grammar || rc=1; \
	bash $(SIM_SCRIPT) --fuzz-mode $(FUZZ_MODE_CASES) || rc=1; \
	bash $(SIM_DOWN_SCRIPT) >/dev/null 2>&1 || true; \
	trap - EXIT INT TERM; \
	exit $$rc

# `auto_sim` is test-sim under the name that pairs with man_sim. Same target,
# so the matrix and this alias can never drift apart -- scripts/run_tests.py
# runs `make test-sim`, and this is that.
auto_sim: test-sim

# ── the hand-driven sandbox ─────────────────────────────────────────────
#
# man_sim is auto_sim's opposite in every way that matters:
#
#              auto_sim (test-sim)         man_sim
#   port       SIM_PORT, out of the way    MAN_SIM_PORT, the conventional one
#   cast       SIM_USERS personas, fixed   MAN_SIM_USERS over 11 channels
#   lifetime   torn down when its probes   stays up until you run
#              finish, pass or fail        `make man_sim-down`
#   purpose    asserting                   exploring
#
# The server binds INADDR_ANY (Server.cpp:386), so it is reachable from any
# machine that can route here. The connect lines printed at the end name this
# host's real addresses rather than assuming localhost, because the whole
# point of this target is that someone else connects to it.
man_sim: $(BIN)
	$(call act,$(C_MAG),SIM   ,manual sandbox $(C_DIM)($(MAN_SIM_USERS) personas, port $(MAN_SIM_PORT), password $(MAN_SIM_PASS))$(C_RST))
	@SIM_DIR=$(MAN_SIM_DIR) bash $(SIM_SCRIPT) --port $(MAN_SIM_PORT) --password $(MAN_SIM_PASS) \
		--roster $(MAN_SIM_ROSTER) --users $(MAN_SIM_USERS) \
		$(if $(filter 1,$(MAN_SIM_CHATTER)),--chatter,--no-scenario) \
		|| { printf '  the sandbox failed to start\n' >&2; exit 1; }
	@printf '\n'
	$(call tag,$(C_GRN),READY ,the sandbox is up — connect to any of these)
	@ip -4 addr show 2>/dev/null | grep -oP 'inet \K[\d.]+' | grep -v '^127\.' \
		| while read -r _a; do \
			printf '    %b\n' "$(C_BLD)/server $$_a $(MAN_SIM_PORT) $(MAN_SIM_PASS)$(C_RST)"; \
		done
	@printf '    %s\n' "$(C_DIM)nc:  { printf 'PASS $(MAN_SIM_PASS)\\r\\nNICK me\\r\\nUSER me 0 * :Me\\r\\nJOIN #general\\r\\n'; cat; } | nc -C <host> $(MAN_SIM_PORT)$(C_RST)"
	@printf '\n'
	$(call act,$(C_DIM),ROOMS ,#general #dev #ops #random #support #games #linux #files #secret #void #lurkers)
	$(call act,$(C_DIM),OPS   ,alice bob peggy — real clients the SERVER gave @)
	$(call act,$(C_DIM),BOTS  ,JokerBot GrumpyBot HappyBot HypeBot SadBot FileBot OpBot CalmBot $(C_DIM)(in-server)$(C_RST))
	$(call act,$(C_DIM),NOTE  ,bots are services like NickServ — they warn and escalate$(C_RST))
	$(call act,$(C_DIM),TRY   ,say something rude in #general and watch them each react differently)
	$(call act,$(C_DIM),WATCH ,make man_sim-logs NICK=alice $(S_DOT) the bots live in the server itself)
	$(call act,$(C_DIM),NEXT  ,make man_sim-status $(S_DOT) make man_sim-logs NICK=alice $(S_DOT) make man_sim-down)

man_sim-status:
	@SIM_DIR=$(MAN_SIM_DIR) bash $(SIM_SCRIPT) --status

# make man_sim-logs NICK=alice   (all personas if NICK is unset)
man_sim-logs:
	@SIM_DIR=$(MAN_SIM_DIR) bash $(SIM_SCRIPT) --logs $(NICK)

man_sim-down:
	$(call act,$(C_YEL),SIM   ,tearing the sandbox down)
	@SIM_DIR=$(MAN_SIM_DIR) SIM_DIR_EXPLICIT=1 bash $(SIM_DOWN_SCRIPT) >/dev/null 2>&1 || true

# ── HEAVY: valgrind ────────────────────────────────────────────────────
# Exit codes are the script's own: 0 clean, 97 a leak, 90 the scripted client
# setup could not be verified (an environment problem, not a leak) -- 90 is
# reported as a skip so a flaky socket does not read as a memory bug.
test-mem: $(BIN)
	$(call act,$(C_MAG),MEM   ,valgrind $(C_DIM)(scripted, tier=$(MEM_TIER), port $(MEM_PORT))$(C_RST))
	@bash $(MEM_SCRIPT) --auto --tier=$(MEM_TIER) $(MEM_PORT) mempass; rc=$$?; \
	if [ $$rc -eq 0 ]; then \
		printf '%b\n' '  $(C_GRN)$(S_OK)$(C_RST)  no leaks'; \
	elif [ $$rc -eq 90 ]; then \
		printf '%b\n' '  $(C_YEL)$(S_DOT)$(C_RST)  SKIP $(C_DIM)- client setup unverified, not a leak result$(C_RST)'; \
		rc=0; \
	else \
		printf '%b\n' '  $(C_YEL)!!$(C_RST)  valgrind reported a problem $(C_DIM)(exit '"$$rc"')$(C_RST)'; \
	fi; \
	exit $$rc

# ── STATIC: cheap gates ────────────────────────────────────────────────
headers:
	$(call act,$(C_MAG),HEADER,every #include names a tracked file)
	@python3 $(HEADERS_SCRIPT)

whitespace:
	$(call act,$(C_MAG),WS    ,trailing whitespace + final newline $(C_DIM)(and the advisory reports)$(C_RST))
	@bash $(WS_SCRIPT) --check

test-list:
	@printf '%b\n' \
	'' \
	'  $(C_BLD)test targets$(C_RST) $(C_DIM)$(S_DOT)$(C_RST) one entry point per suite; aggregates run them all' \
	'' \
	'  $(C_YEL)AGGREGATES$(C_RST)' \
	'    $(C_GRN)matrix$(C_RST)         $(C_BLD)every suite, in parallel, behind a live table$(C_RST)' \
	'                   $(C_DIM)build gates run serially first (they run make re), then$(C_RST)' \
	'                   $(C_DIM)the rest run at once against a staged binary.$(C_RST)' \
	'    $(C_GRN)matrix-quick$(C_RST)   ...without the two slow ones $(C_DIM)(sim, mem)$(C_RST)' \
	'    $(C_GRN)matrix-list$(C_RST)    what the matrix would run' \
	'    $(C_GRN)check$(C_RST)          $(C_DIM)serial: $(CHECK_PHASES)$(C_RST)' \
	'    $(C_GRN)test-all$(C_RST)       check + the two slow ones $(C_DIM)(sim, mem)$(C_RST)' \
	'    $(C_GRN)test-quick$(C_RST)     $(C_DIM)$(QUICK_PHASES)$(C_RST)' \
	'' \
	'  $(C_YEL)LIVE$(C_RST) $(C_DIM)- drive a real server over TCP$(C_RST)' \
	'    $(C_GRN)test-unit$(C_RST)      Google Test, in-process     $(C_DIM)(= make test)$(C_RST)' \
	'    $(C_GRN)test-shell$(C_RST)     black-box shell suite       $(C_DIM)port $(IRC_PORT)$(C_RST)' \
	'    $(C_GRN)test-grammar$(C_RST)   RFC 2812 conformance + fuzz $(C_DIM)ports $(GRAMMAR_PORT)/$(FUZZ_PORT)$(C_RST)' \
	'' \
	'  $(C_YEL)HEAVY$(C_RST)' \
	'    $(C_GRN)auto_sim$(C_RST)       populated simulation + 3 probes $(C_DIM)port $(SIM_PORT) (= test-sim)$(C_RST)' \
	'    $(C_GRN)test-mem$(C_RST)       valgrind, scripted clients      $(C_DIM)port $(MEM_PORT)$(C_RST)' \
	'' \
	'  $(C_YEL)SANDBOX$(C_RST) $(C_DIM)- a populated server for YOU, not for assertions$(C_RST)' \
	'    $(C_GRN)man_sim$(C_RST)        $(MAN_SIM_USERS) autonomous clients, 11 channels $(C_DIM)port $(MAN_SIM_PORT), password $(MAN_SIM_PASS)$(C_RST)' \
	'    $(C_GRN)man_sim-status$(C_RST) who is up and where' \
	'    $(C_GRN)man_sim-logs$(C_RST)   $(C_DIM)NICK=alice$(C_RST) what a persona received' \
	'    $(C_GRN)man_sim-down$(C_RST)   free the port again' \
	'    $(C_DIM)its own state dir ($(MAN_SIM_DIR)/), so it runs alongside the matrix$(C_RST)' \
	'    $(C_DIM)the 8 bots are compiled into ircserv $(S_DOT) see include/bots/ABot.hpp$(C_RST)' \
	'' \
	'  $(C_YEL)STATIC$(C_RST) $(C_DIM)- no server, no network$(C_RST)' \
	'    $(C_GRN)norm$(C_RST)  $(C_GRN)evloop$(C_RST)  $(C_GRN)evloop-run$(C_RST)  $(C_GRN)audit$(C_RST)  $(C_GRN)headers$(C_RST)  $(C_GRN)whitespace$(C_RST)' \
	'' \
	'  $(C_YEL)KNOBS$(C_RST) $(C_DIM)- every port is overridable, so two runs never collide$(C_RST)' \
	'    $(C_DIM)make check IRC_PORT=7001$(C_RST)' \
	'    $(C_DIM)make test-unit UNIT_ARGS=--gtest_filter=Channel*$(C_RST)' \
	'    $(C_DIM)make test-shell SHELL_ARGS="--only 05"$(C_RST)' \
	'    $(C_DIM)make test-grammar FUZZ_CASES=5000 FUZZ_SEED=42$(C_RST)' \
	'    $(C_DIM)make matrix RUNNER_ARGS="--only unit,grammar --jobs 2"$(C_RST)' \
	'    $(C_DIM)make matrix RUNNER_ARGS=--ascii$(C_RST)  $(C_DIM)(no emoji)$(C_RST)' \
	''

NORM_LIBCPP_NAMES	= $(LIBCPP_CORE_NAMES) $(LIBCPP_FULL_NAMES)
NORM_FILES		= src include

NORM_TIDY_FLAGS	= -std=c++98 $(INCLUDES)

norm:
	$(call act,$(C_MAG),NORM  ,clang-format + clang-tidy + cpplint + cppcheck)
	@PATH="$$HOME/.local/bin:$$PATH" python3 $(NORM_SCRIPT) $(NORM_FILES) \
		-- $(NORM_TIDY_FLAGS)

norm-fix:
	$(call act,$(C_MAG),FMT   ,clang-format -i over src/ and include/)
	@PATH="$$HOME/.local/bin:$$PATH" clang-format -i \
		$$(find src include -name '*.cpp' -o -name '*.hpp') \
		$(filter-out src include,$(NORM_FILES))
	@$(PR_MSG) '%b\n' '$(C_GRN)$(S_OK)$(C_RST)  clang-format applied $(C_DIM)$(S_ARR) re-run: make norm$(C_RST)'

evloop:
	$(call act,$(C_MAG),EVLOOP,one event wait $(C_DIM)- and no socket I/O behind its back$(C_RST))
	@python3 $(EVLOOP_SCRIPT)

evloop-run: $(BIN)
	$(call act,$(C_MAG),EVLOOP,static + strace of the live server)
	@python3 $(EVLOOP_SCRIPT) --runtime --binary $(BIN)

audit:
	$(call act,$(C_MAG),AUDIT ,subject compliance $(C_DIM)- three tiers, tokens, syscalls, loop$(C_RST))
	@bash $(AUDIT_SCRIPT)

testclean:
	$(call act,$(C_YEL),CLEAN ,test artifacts $(C_DIM)(tests/)$(C_RST))
	@$(MAKE) -C tests fclean


help:
	@printf '%b\n' \
	'' \
	'  $(C_BLD)ft_irc$(C_RST) $(C_DIM)$(S_DOT)$(C_RST) IRC server in C++98 (RFC 2812), epoll-driven, single-threaded.' \
	'  $(C_DIM)usage:$(C_RST) make $(C_DIM)[target] [VAR=value]$(C_RST)' \
	'' \
	'  $(C_YEL)BUILD TIERS$(C_RST) $(C_DIM)— same kernel sources; they differ only in what is linked.$(C_RST)' \
	'    $(C_GRN)all$(C_RST)            $(C_DIM)(default)$(C_RST) full tier: bonus + platform extras (' \
	'                   fancy console). The extras are runtime-gated by' \
	'                   FT_IRC_CONFIG, so with no config file this binary behaves' \
	'                   byte-identically to the bonus tier.' \
	'    $(C_GRN)bonus$(C_RST)          mandatory + the subject bonus: Bot (ircbot) and FILE transfer.' \
	'    $(C_GRN)mandatory$(C_RST)      strictly the subject mandatory part — the pure RFC kernel.' \
	'                   $(C_BLD)Defend on this binary.$(C_RST)' \
	'    $(C_GRN)verify-tiers$(C_RST)   build all three in strict sequence — the safe cross-tier' \
	'                   -Werror check. Never build tiers concurrently: unbounded' \
	'                   parallel builds swap-freeze low-headroom machines.' \
	'' \
	'  $(C_YEL)BUILD LAYOUT$(C_RST) $(C_DIM)— every generated file lives under build/$(C_RST)' \
	'    $(C_CYA)build/obj/$$(TIER)/$(C_RST)   .o and .d, mirroring src/ (plus libcpp/, libcpp98/).' \
	'    $(C_CYA)build/obj/tests/$(C_RST)     the Google Test suite objects.' \
	'    $(C_CYA)build/bin/$(C_RST)           $(NAME) and test_runner.' \
	'    $(C_DIM)nothing generated is written to the repo root — no ./$(NAME).$(C_RST)' \
	'' \
	'  $(C_YEL)RUN$(C_RST)' \
	'    ./$(BIN) <port> <password>  $(C_DIM)e.g. ./$(BIN) 6667 mypass$(C_RST)' \
	'    nc -C 127.0.0.1 6667                 $(C_DIM)manual smoke test: PASS / NICK / USER / JOIN$(C_RST)' \
	'' \
	'  $(C_YEL)TESTS$(C_RST) $(C_DIM)— every suite in the repo has a target; make test-list for the lot$(C_RST)' \
	'    $(C_GRN)matrix$(C_RST)         $(C_BLD)run everything in parallel behind a live status table.$(C_RST)' \
	'                   Build gates go first, serially, because they run make re;' \
	'                   the rest then run at once against a staged binary.' \
	'    $(C_GRN)matrix-quick$(C_RST)   ...minus the two slow suites (sim, mem).' \
	'    $(C_GRN)check$(C_RST)          the same suites, serially — slower, but one suite'"'"'s' \
	'                   output at a time, which is what you want when reading a' \
	'                   failure rather than counting them.' \
	'    $(C_GRN)test$(C_RST)           the Google Test suite alone $(C_DIM)(= test-unit)$(C_RST). Single case:' \
	'                   $(C_DIM)make test-unit UNIT_ARGS=--gtest_filter=Channel*$(C_RST)' \
	'    $(C_GRN)test-list$(C_RST)      every test target, its port, and the knobs.' \
	'    $(C_GRN)testclean$(C_RST)      remove the test build artifacts.' \
	'' \
	'  $(C_YEL)LIBRARY$(C_RST) $(C_DIM)— vendor/libcpp$(C_RST)' \
	'    ircserv $(C_BLD)compiles$(C_RST) the C++98-clean libcpp modules into itself and links' \
	'    plain object files. No .a is linked, so nothing on the link line can be' \
	'    read as an external library $(C_DIM)(subject.txt:91)$(C_RST). The module list lives in' \
	'    LIBCPP_CORE_NAMES / LIBCPP_FULL_NAMES / LIBCPP98_NAMES in this file.' \
	'' \
	'    libcpp also builds standalone, for its other consumers:' \
	'      $(C_DIM)make -C $(LIBCPP) c98$(C_RST)   $(C_DIM)28 C++98-clean modules -> libftpp98.a$(C_RST)' \
	'      $(C_DIM)make -C $(LIBCPP)$(C_RST)       $(C_DIM)all 40, C++17          -> libftpp.a/.so$(C_RST)' \
	'    ircserv does not consume either archive.' \
	'' \
	'    Any umbrella header (libcpp/libcpp.hpp, net/network.hpp, ...) is safe to' \
	'    include from C++98: they gate their C++11-only modules on' \
	'    LIBCPP_HAS_CXX11 from libcpp/config.hpp rather than failing to parse.' \
	'' \
	'  $(C_YEL)QUALITY GATES$(C_RST)' \
	'    $(C_GRN)norm$(C_RST)           clang-format + clang-tidy + cpplint + cppcheck. A tool that is' \
	'                   not installed is reported skipped, not failed.' \
	'    $(C_GRN)norm-fix$(C_RST)       apply the mechanical half of norm (clang-format -i).' \
	'                   $(C_DIM)Advisory only: .clang-format cannot reproduce two house$(C_RST)' \
	'                   $(C_DIM)conventions, so a diff does not mean a file is wrong.$(C_RST)' \
	'    $(C_GRN)evloop$(C_RST)         assert the one rule the subject grades zero: no recv/send/' \
	'                   accept without a readiness event. Same shape as bircd.' \
	'    $(C_GRN)evloop-run$(C_RST)     ...and strace the live server to prove it at runtime.' \
	'    $(C_GRN)audit$(C_RST)          every subject-compliance gate: three tiers, C++98 tokens,' \
	'                   forbidden calls, one event wait, Makefile rules, relink.' \
	'' \
	'  $(C_YEL)HOUSEKEEPING$(C_RST)' \
	'    $(C_GRN)clean$(C_RST)          remove build/obj/ (all tiers, tests included).' \
	'    $(C_GRN)fclean$(C_RST)         clean + build/ (and a legacy root ./$(NAME), if any).' \
	'    $(C_GRN)re$(C_RST)             fclean, then a full build.    $(C_GRN)help$(C_RST)     this screen.' \
	'' \
	'  $(C_YEL)OVERRIDABLE VARIABLES$(C_RST) $(C_DIM)— pass on the command line: make <target> VAR=value$(C_RST)' \
	'    $(C_CYA)TIER$(C_RST)=full|bonus|mandatory' \
	'                   which source set to link. Prefer the named targets above;' \
	'                   build/obj/$$(TIER)/ keeps the three object sets apart, and' \
	'                   build/obj/.tier_* forces the relink when you switch tiers.' \
	'                   $(C_DIM)current: $(TIER)$(C_RST)' \
	'    $(C_CYA)CXX$(C_RST)            the compiler.  $(C_DIM)current: $(CXX)$(C_RST)' \
	'    $(C_CYA)CXXFLAGS$(C_RST)       $(C_DIM)current: $(CXXFLAGS)$(C_RST)' \
	'                   Overriding $(C_BLD)replaces$(C_RST) the lot, and the subject mandates' \
	'                   -Wall -Wextra -Werror -std=c++98. To add, re-state them:' \
	'                     $(C_DIM)make re CXXFLAGS="$$(CXXFLAGS) -g -O0"$(C_RST)' \
	'    $(C_CYA)V$(C_RST)=1            echo the real compiler command lines instead of the' \
	'                   short CXX/LINK tags.' \
	'    $(C_CYA)COLOR$(C_RST)=0|1      force colour off / on. Default auto: on only when' \
	'                   stdout is a terminal, so piped and CI logs stay plain ASCII.' \
	'    $(C_CYA)NO_COLOR$(C_RST)=1     same as COLOR=0 $(C_DIM)(no-color.org convention)$(C_RST).' \
	'    $(C_DIM)make -s$(C_RST)        drop the decoration entirely (compiler output only).' \
	'' \
	'  $(C_YEL)RUNTIME ENVIRONMENT$(C_RST) $(C_DIM)— read by the binary, not by make$(C_RST)' \
	'    $(C_CYA)FT_IRC_CONFIG$(C_RST)=<path.ini>' \
	'                   enables the full-tier extras: [audit]' \
	'                   the settings override. Unset, the full binary behaves' \
	'                   like bonus apart from the console sink.' \
	'' \
	'  $(C_YEL)OUT-OF-BAND$(C_RST) $(C_DIM)— everything else now has a target; these do not$(C_RST)' \
	'    bash scripts/normalize.sh              $(C_DIM)APPLY whitespace fixes (make whitespace only checks)$(C_RST)' \
	'    bash scripts/simulation.sh             $(C_DIM)a populated server you can join yourself$(C_RST)' \
	'    bash scripts/shutdown_simulation.sh    $(C_DIM)...and free it again$(C_RST)' \
	'    docker compose up --build              $(C_DIM)ircserv + the ai-assistant companion$(C_RST)' \
	''

.PHONY: all bonus mandatory build clean fclean re testclean verify-tiers \
	norm norm-fix evloop evloop-run audit help \
	check test-all test-quick test-list matrix matrix-quick matrix-list \
	test test-unit test-shell test-grammar test-sim test-mem \
	auto_sim man_sim man_sim-status man_sim-logs man_sim-down \
	headers whitespace
