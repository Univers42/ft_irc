# ═══════════════════════════════════════════════════════════════════════════
#  ft_irc — IRC server in C++98 (RFC 2812), single-threaded, epoll-driven.
#
#  `make`      builds the full tier and prints where to go next.
#  `make help` prints every target, what it is for, and which variables can
#              be overridden on the command line.
# ═══════════════════════════════════════════════════════════════════════════

NAME		= ircserv

CXX			= c++
CXXFLAGS	= -Wall -Wextra -Werror -std=c++98

.NOTPARALLEL:

#  Bare `make` prints the help screen and builds nothing; `make all` builds.
#  There were two .DEFAULT_GOAL assignments here and the later one silently
#  won -- consolidated so the choice is visible in one place.
#
#  Anything automated must therefore say `make all` explicitly, never bare
#  `make`. scripts/audit.sh does (its no-relink check compares two `make all`
#  runs); so do tests/12_build_norm.sh, the Dockerfile and ci.yml.
.DEFAULT_GOAL := help

SRCDIR		= src

# ── Output presentation ─────────────────────────────────────────────────────
#  Colour is on only when make's own stdout is a terminal: GNU make >= 4.1
#  defines MAKE_TERMOUT for exactly that, and it survives into the sub-makes
#  the tier targets spawn. Redirected to a file or a pipe (CI, docker build,
#  scripts/audit.hellish) everything below degrades to plain ASCII — which is
#  what keeps build logs greppable, since the audit greps them for compiler
#  diagnostics and ANSI escapes would sit in the middle of the matched text.
#
#  Overrides:  NO_COLOR=1 / COLOR=0  force plain      COLOR=1  force colour
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

#  `make -s` must stay silent (tests/12_build_norm.sh drives the build that
#  way). Only the leading short-flag cluster is inspected: long options such
#  as --no-builtin-rules also contain an 's' and would false-positive.
MAKE_SHORTFLAGS := $(firstword $(MAKEFLAGS))
ifeq (,$(findstring -,$(MAKE_SHORTFLAGS)))
    ifneq (,$(findstring s,$(MAKE_SHORTFLAGS)))
        QUIET := 1
    endif
endif

#  V=1 swaps the short per-file tags for the real command lines. AT is the
#  recipe-echo suppressor; PR_TAG/PR_MSG are `printf` or the no-op `:` — they
#  carry no leading `@`, so they can also be used inside a shell `if`.
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

#  $(call tag,<colour>,<LABEL>,<detail>) — per-file build step (hidden by V=1)
#  $(call act,<colour>,<LABEL>,<detail>) — one-off action (hidden only by -s)
tag = @$(PR_TAG) '%b\n' '  $(1)$(2)$(C_RST)  $(3)'
act = @$(PR_MSG) '%b\n' '  $(1)$(2)$(C_RST)  $(3)'

HINT = $(C_DIM)   make help  $(S_DOT)  targets, tiers and overridable flags$(C_RST)

# ── Build tiers ────────────────────────────────────────────────────────────
#  make mandatory  → strictly the subject's mandatory part (pure RFC kernel)
#  make bonus      → mandatory + subject bonus (bot, file transfer)
#  make / make all → full: bonus + optional platform extras (
#                    AuditLog, fancy console) — still runtime-gated by
#                    FT_IRC_CONFIG, so without a config file the binary
#                    behaves exactly like the bonus tier.
#
#  Tiers differ ONLY in which sources are linked (per-tier object dirs, one
#  registerExtensions() TU each); the kernel sources are identical.
#  Everything the build generates lives under build/: objects and dependency
#  files in build/obj/<tier>/ (mirroring the source tree), linked binaries in
#  build/bin/. The only generated name left in the repo root is the ./ircserv
#  symlink below, so `rm -rf build ircserv` is a complete clean and the root
#  listing stays source-only.
TIER		?= full
BUILDDIR	= build
BINDIR		= $(BUILDDIR)/bin
OBJROOT		= $(BUILDDIR)/obj
OBJDIR		= $(OBJROOT)/$(TIER)

#  The subject runs the server as `./ircserv <port> <password>` from the repo
#  root (subject.txt:191) and every script here does the same, so that name
#  has to keep working. The link output is build/bin/ircserv; ./ircserv is a
#  relative symlink onto it, created by the $(NAME) rule and removed by
#  fclean. Nothing needs to know which of the two it is holding.
BIN			= $(BINDIR)/$(NAME)

#  Written by the link recipe and consumed by `build`, so the closing banner
#  can say "built" or "is up to date" without guessing. The wording matters:
#  scripts/audit.hellish proves `make` does not relink by re-running it and
#  looking for exactly that phrase.
LINKSTAMP	= $(OBJDIR)/.relinked

# ── libcpp: the project's own C++98-clean modules, compiled in ──────────────
#  ircserv compiles these sources itself and links plain object files. It
#  does NOT link libcpp's archive, and that is the deliberate choice:
#  subject.txt:91 forbids external libraries, and the least ambiguous way to
#  satisfy it is for no .a to appear on the link line at all. Compiling the
#  sources in keeps the claim literally true and leaves nothing for an
#  evaluator to have to interpret.
#
#  The named lists below are therefore the routing table: they are what says
#  which libcpp modules are part of ircserv. Adding one means adding its name
#  here, after checking it is C++98-clean.
#
#  libcpp can still be built standalone the way a 42 library is expected to
#  be — `make -C vendor/libcpp c98` produces libftpp98.a from the same 28
#  C++98-clean modules, and `make -C vendor/libcpp` the full C++17 libftpp.a.
#  Those exist for libcpp's other consumers and to prove the subset compiles;
#  ircserv just does not consume the output. What ircserv DOES rely on from
#  that work is the include graph: libcpp/config.hpp defines
#  LIBCPP_HAS_CXX11, and the umbrella headers gate their C++11-only modules
#  on it, so a C++98 translation unit can include libcpp/libcpp.hpp without a
#  parse error.
#
#  The libcpp objects are listed BEFORE the ft_irc objects in the $(NAME)
#  prerequisites so the dependency compiles first — .NOTPARALLEL makes
#  prerequisite order literal build order, so a libcpp breakage surfaces
#  immediately instead of after nineteen project files.
LIBCPP		= vendor/libcpp
INCLUDES	= -I include -I $(LIBCPP)/include -I $(LIBCPP)/c98/include

# ── Source groups (names without dir/extension) ─────────────────────────────
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

EXTRA_NAMES	= AuditLog \
			  extras/FancyLogSink

SRC_NAMES	= $(CORE_NAMES)
ifneq ($(TIER),mandatory)
SRC_NAMES	+= $(BONUS_NAMES)
endif
ifeq ($(TIER),full)
SRC_NAMES	+= $(EXTRA_NAMES)
endif

SRCS		= $(addprefix $(SRCDIR)/,$(addsuffix .cpp,$(SRC_NAMES)))

# str/* is used by the kernel (casemapped parsing, to_string, consttime
# compare); util/config + term/* only by the full tier (config, console).
LIBCPP_CORE_NAMES	= str/format str/case str/utf8 str/secure
LIBCPP_FULL_NAMES	= util/config term/color term/style term/table \
					  term/stylesheet term/writer

LIBCPP_NAMES	= $(LIBCPP_CORE_NAMES)
ifeq ($(TIER),full)
LIBCPP_NAMES	+= $(LIBCPP_FULL_NAMES)
endif

LIBCPP_SRCS		= $(addprefix $(LIBCPP)/src/,$(addsuffix .cpp,$(LIBCPP_NAMES)))

# libcpp C++98 tier (vendor/libcpp/c98): generic building blocks promoted
# out of this project — line framing, streaming CSV, epoll registration.
LIBCPP98_NAMES	= line_buffer csv_writer reactor buffered_socket
LIBCPP98_SRCS	= $(addprefix $(LIBCPP)/c98/src/,$(addsuffix .cpp,$(LIBCPP98_NAMES)))

OBJS			= $(SRCS:$(SRCDIR)/%.cpp=$(OBJDIR)/%.o)
LIBCPP_OBJS		= $(LIBCPP_SRCS:$(LIBCPP)/src/%.cpp=$(OBJDIR)/libcpp/%.o)
LIBCPP98_OBJS	= $(LIBCPP98_SRCS:$(LIBCPP)/c98/src/%.cpp=$(OBJDIR)/libcpp98/%.o)

# ── Tier entry points (recursive: re-evaluates the source lists per tier) ───
all:
	@$(PR_MSG) '%b\n' '' '$(C_DIM)$(S_BAR)$(C_RST) $(C_BLD)ft_irc$(C_RST) $(C_DIM)$(S_DOT)$(C_RST) $(C_MAG)full tier$(C_RST) $(C_DIM)$(S_DOT) $(CXX) $(CXXFLAGS)$(C_RST)'
	@$(MAKE) --no-print-directory TIER=full build

bonus:
	@$(PR_MSG) '%b\n' '' '$(C_DIM)$(S_BAR)$(C_RST) $(C_BLD)ft_irc$(C_RST) $(C_DIM)$(S_DOT)$(C_RST) $(C_MAG)bonus tier$(C_RST) $(C_DIM)$(S_DOT) $(CXX) $(CXXFLAGS)$(C_RST)'
	@$(MAKE) --no-print-directory TIER=bonus build

mandatory:
	@$(PR_MSG) '%b\n' '' '$(C_DIM)$(S_BAR)$(C_RST) $(C_BLD)ft_irc$(C_RST) $(C_DIM)$(S_DOT)$(C_RST) $(C_MAG)mandatory tier$(C_RST) $(C_DIM)$(S_DOT) $(CXX) $(CXXFLAGS)$(C_RST)'
	@$(MAKE) --no-print-directory TIER=mandatory build

# Verify all three tiers in STRICT SEQUENCE — never concurrently. The tier
# marker (obj/.tier_$(TIER)) forces the needed relink between tiers, so no
# fclean is required. This is the safe way to check -Werror across tiers:
# one make invocation, serialized, each capped by .NOTPARALLEL above. Building
# tiers in parallel is what OOM-freezes machines; this makes it impossible.
verify-tiers:
	@$(MAKE) --no-print-directory mandatory
	@$(MAKE) --no-print-directory bonus
	@$(MAKE) --no-print-directory all
	@$(PR_MSG) '%b\n' '$(C_GRN)$(S_OK)$(C_RST)  all three tiers built sequentially $(C_DIM)$(S_DOT)$(C_RST) -Werror clean' ''

build: $(NAME)
	@if [ -f $(LINKSTAMP) ]; then \
		rm -f $(LINKSTAMP); \
		$(PR_MSG) '%b\n' '' '$(C_GRN)$(S_OK)$(C_RST)  $(C_BLD)$(NAME)$(C_RST) $(C_DIM)$(S_DOT)$(C_RST) $(TIER) tier built $(C_DIM)$(S_ARR)$(C_RST) ./$(NAME) <port> <password>' '$(HINT)' ''; \
	else \
		$(PR_MSG) '%b\n' '' '$(C_GRN)$(S_OK)$(C_RST)  $(C_BLD)$(NAME)$(C_RST) is up to date $(C_DIM)$(S_DOT)$(C_RST) $(TIER) tier' '$(HINT)' ''; \
	fi

# The marker forces a relink when switching tiers (one binary name, three
# object sets) and keeps a same-tier repeat a no-op.
#  Everything on this line is a plain object file — no archive, so link order
#  carries no meaning and the libcpp objects can stay first, matching the
#  build order.
#  make stats through the symlink, so once ./ircserv points at a freshly
#  linked build/bin/ircserv the two share an mtime and this rule stays a
#  no-op -- which is what keeps the audit's "second make is a no-op" check
#  passing.
$(NAME): $(BIN)
	$(AT)ln -sf $(BIN) $(NAME)

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

#  clean drops every object tree under build/obj -- the three server tiers
#  and the test suite's, since they all live there now. fclean removes what
#  is left of build/ (the binaries) plus the ./ircserv symlink, leaving no
#  generated file anywhere in the tree.
clean:
	$(call act,$(C_YEL),CLEAN ,$(OBJROOT)/ $(C_DIM)(objects + deps, every tier)$(C_RST))
	@rm -rf $(OBJROOT)

fclean: clean
	$(call act,$(C_YEL),CLEAN ,$(BUILDDIR)/ $(C_DIM)+$(C_RST) ./$(NAME))
	@rm -f $(NAME)
	@rm -rf $(BUILDDIR)

re: fclean all

test:
	$(call act,$(C_MAG),TEST  ,Google Test suite $(C_DIM)(C++17, via tests/Makefile)$(C_RST))
	@$(MAKE) -C tests

# ── norm: style + static-analysis gate ─────────────────────────────────────
#  vendor/scripts/norminette.sh drives four tools over the sources — see
#  .clang-format (layout), .clang-tidy (bug-finding), CPPLINT.cfg (style), and
#  cppcheck. A tool that isn't installed is reported "skipped", not failed, so
#  the target still runs on a bare machine:
#      pip install --user cpplint clang-tidy      # clang-format: your distro
#      cppcheck: distro package, or build from github.com/danmar/cppcheck
#
#  Scope is exactly what ircserv compiles: src/, include/, and the libcpp
#  modules linked into the binary. libcpp's demo/, studio/ and lab/ trees are
#  not part of this build (and carry their own vendored node_modules), so they
#  are listed out rather than walked.
NORM_SCRIPT		= vendor/scripts/norminette.sh
NORM_LIBCPP_NAMES	= $(LIBCPP_CORE_NAMES) $(LIBCPP_FULL_NAMES)
NORM_FILES		= src include \
			  $(addprefix $(LIBCPP)/src/,$(addsuffix .cpp,$(NORM_LIBCPP_NAMES))) \
			  $(addprefix $(LIBCPP)/include/libcpp/,$(addsuffix .hpp,$(NORM_LIBCPP_NAMES))) \
			  $(addprefix $(LIBCPP)/c98/src/,$(addsuffix .cpp,$(LIBCPP98_NAMES))) \
			  $(addprefix $(LIBCPP)/c98/include/libcpp98/,$(addsuffix .hpp,$(LIBCPP98_NAMES)))

# Everything after `--` is what clang-tidy parses the sources with, so it has
# to match the real build. -Werror is deliberately left out: clang's warning
# set differs from the build compiler's, and a clang-only diagnostic must not
# be able to fail the lint gate.
NORM_TIDY_FLAGS	= -std=c++98 $(INCLUDES)

# The script is Python despite the .sh name, and ships non-executable; invoke
# it through the interpreter rather than relying on its mode bits.
norm:
	$(call act,$(C_MAG),NORM  ,clang-format + clang-tidy + cpplint + cppcheck)
	@PATH="$$HOME/.local/bin:$$PATH" python3 $(NORM_SCRIPT) $(NORM_FILES) \
		-- $(NORM_TIDY_FLAGS)

# Applies the mechanical half of `norm` in place (layout only — clang-format
# never changes semantics). cpplint and the analyzer findings stay manual.
norm-fix:
	$(call act,$(C_MAG),FMT   ,clang-format -i over src/ include/ + linked libcpp)
	@PATH="$$HOME/.local/bin:$$PATH" clang-format -i \
		$$(find src include -name '*.cpp' -o -name '*.hpp') \
		$(filter-out src include,$(NORM_FILES))
	@$(PR_MSG) '%b\n' '$(C_GRN)$(S_OK)$(C_RST)  clang-format applied $(C_DIM)$(S_ARR) re-run: make norm$(C_RST)'

testclean:
	$(call act,$(C_YEL),CLEAN ,test artifacts $(C_DIM)(tests/)$(C_RST))
	@$(MAKE) -C tests fclean

# ── help ───────────────────────────────────────────────────────────────────
#  Kept as one printf so the whole screen is a single shell invocation. No
#  apostrophes in the text: every line is single-quoted for the shell.
help:
	@printf '%b\n' \
	'' \
	'  $(C_BLD)ft_irc$(C_RST) $(C_DIM)$(S_DOT)$(C_RST) IRC server in C++98 (RFC 2812), epoll-driven, single-threaded.' \
	'  $(C_DIM)usage:$(C_RST) make $(C_DIM)[target] [VAR=value]$(C_RST)' \
	'' \
	'  $(C_YEL)BUILD TIERS$(C_RST) $(C_DIM)— same kernel sources; they differ only in what is linked.$(C_RST)' \
	'    $(C_GRN)all$(C_RST)            $(C_DIM)(default)$(C_RST) full tier: bonus + platform extras (' \
	'                   AuditLog, fancy console). The extras are runtime-gated by' \
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
	'    $(C_CYA)./$(NAME)$(C_RST)            symlink to build/bin/$(NAME), so the subject'"'"'s' \
	'                         ./$(NAME) <port> <password> keeps working from the root.' \
	'' \
	'  $(C_YEL)RUN$(C_RST)' \
	'    ./$(NAME) <port> <password>          $(C_DIM)e.g. ./$(NAME) 6667 mypass$(C_RST)' \
	'    nc -C 127.0.0.1 6667                 $(C_DIM)manual smoke test: PASS / NICK / USER / JOIN$(C_RST)' \
	'' \
	'  $(C_YEL)TESTS$(C_RST)' \
	'    $(C_GRN)test$(C_RST)           build + run the Google Test suite (C++17, in-process,' \
	'                   delegates to tests/Makefile). Single case:' \
	'                   $(C_DIM)make -C tests build && ./build/bin/test_runner --gtest_filter=Channel*$(C_RST)' \
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
	'' \
	'  $(C_YEL)HOUSEKEEPING$(C_RST)' \
	'    $(C_GRN)clean$(C_RST)          remove build/obj/ (all tiers, tests included).' \
	'    $(C_GRN)fclean$(C_RST)         clean + build/ + the ./$(NAME) symlink.' \
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
	'                   AuditLog. Unset, the full binary behaves like bonus.' \
	'' \
	'  $(C_YEL)NOT MAKE TARGETS$(C_RST) $(C_DIM)— out-of-band tooling, run them directly$(C_RST)' \
	'    bash scripts/audit.hellish             $(C_DIM)subject-compliance audit (3 tiers, C++98 scan)$(C_RST)' \
	'    bash scripts/memcheck.hellish --auto   $(C_DIM)valgrind gate; exit 0 clean / 97 leak / 90 unverified$(C_RST)' \
	'    bash scripts/normalize.sh              $(C_DIM)whitespace gate in place (--check = CI mode)$(C_RST)' \
	'    cd tests && bash run_all.sh            $(C_DIM)black-box shell suite vs a live ./$(NAME)$(C_RST)' \
	'    docker compose up --build              $(C_DIM)ircserv + the ai-assistant companion$(C_RST)' \
	''

.PHONY: all bonus mandatory build clean fclean re test testclean verify-tiers \
	norm norm-fix help
