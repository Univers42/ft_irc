NAME		= ircserv

CXX			= c++
CXXFLAGS	= -Wall -Wextra -Werror -std=c++98

.NOTPARALLEL:

SRCDIR		= src

# ── Build tiers ────────────────────────────────────────────────────────────
#  make mandatory  → strictly the subject's mandatory part (pure RFC kernel)
#  make bonus      → mandatory + subject bonus (bot, file transfer)
#  make / make all → full: bonus + optional platform extras (PlatformBus,
#                    AuditLog, fancy console) — still runtime-gated by
#                    FT_IRC_CONFIG, so without a config file the binary
#                    behaves exactly like the bonus tier.
#
#  Tiers differ ONLY in which sources are linked (per-tier object dirs, one
#  registerExtensions() TU each); the kernel sources are identical.
TIER		?= full
OBJDIR		= obj/$(TIER)

# ── libcpp: reuse the project's own C++98-clean modules ──────────────────────
#  These sources compile cleanly under -std=c++98 -Wall -Wextra -Werror; they
#  are built from source as part of ircserv (no external library is linked).
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
			  IrcCase \
			  CommandRegistration \
			  CommandChannel \
			  CommandMessaging \
			  CommandOperator \
			  CommandQuery

BONUS_NAMES	= Bot \
			  bonus/FileTransferExt

EXTRA_NAMES	= PlatformBus \
			  AuditLog \
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
	@$(MAKE) --no-print-directory TIER=full build

bonus:
	@$(MAKE) --no-print-directory TIER=bonus build

mandatory:
	@$(MAKE) --no-print-directory TIER=mandatory build

# Verify all three tiers in STRICT SEQUENCE — never concurrently. The tier
# marker (obj/.tier_$(TIER)) forces the needed relink between tiers, so no
# fclean is required. This is the safe way to check -Werror across tiers:
# one make invocation, serialized, each capped by MAKEFLAGS above. Building
# tiers in parallel is what OOM-freezes machines; this makes it impossible.
verify-tiers:
	@$(MAKE) --no-print-directory mandatory
	@$(MAKE) --no-print-directory bonus
	@$(MAKE) --no-print-directory all
	@echo "\n══════ All three tiers built sequentially (-Werror clean) ══════\n"

build: $(NAME)

# The marker forces a relink when switching tiers (one binary name, three
# object sets) and keeps a same-tier repeat a no-op.
$(NAME): obj/.tier_$(TIER) $(OBJS) $(LIBCPP_OBJS) $(LIBCPP98_OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) $(LIBCPP_OBJS) $(LIBCPP98_OBJS) -o $(NAME)

obj/.tier_$(TIER):
	@mkdir -p obj
	@rm -f obj/.tier_*
	@touch $@

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(OBJDIR)/libcpp/%.o: $(LIBCPP)/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(OBJDIR)/libcpp98/%.o: $(LIBCPP)/c98/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

-include $(OBJS:.o=.d) $(LIBCPP_OBJS:.o=.d) $(LIBCPP98_OBJS:.o=.d)

clean:
	rm -rf obj

fclean: clean
	rm -f $(NAME)

re: fclean all

test:
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
# set differs from the build compiler's, and a clang-only warning must not be
# able to fail the lint gate.
NORM_TIDY_FLAGS	= -std=c++98 $(INCLUDES)

# The script is Python despite the .sh name, and ships non-executable; invoke
# it through the interpreter rather than relying on its mode bits.
norm:
	@PATH="$$HOME/.local/bin:$$PATH" python3 $(NORM_SCRIPT) $(NORM_FILES) \
		-- $(NORM_TIDY_FLAGS)

# Applies the mechanical half of `norm` in place (layout only — clang-format
# never changes semantics). cpplint and the analyzer findings stay manual.
norm-fix:
	@PATH="$$HOME/.local/bin:$$PATH" clang-format -i \
		$$(find src include -name '*.cpp' -o -name '*.hpp') \
		$(filter-out src include,$(NORM_FILES))
	@echo "clang-format applied; re-run 'make norm' to confirm."

testclean:
	@$(MAKE) -C tests fclean

.PHONY: all bonus mandatory build clean fclean re test testclean verify-tiers \
	norm norm-fix
