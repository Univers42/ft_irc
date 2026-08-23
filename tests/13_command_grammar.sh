#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# 13_command_grammar.sh — per-command RFC 2812 conformance, plus a fuzz pass.
#
# The other numbered suites drive the server the way a client would. This one
# drives it the way a *protocol* would: every command production in RFC 2812
# gets its arity, its optional elements, its colon forms and its repetition
# bounds exercised deliberately, and then the same productions get mutated
# and replayed to see what falls over.
#
# Both halves start their own short-lived server on their own port, so this
# script is independent of the long-lived one run_all.sh keeps: it can be run
# on its own, and it cannot disturb the suites around it.
#
#   FUZZ_CASES=n   how hard the fuzz pass works (default 600)
#   FUZZ_SEED=n    replay one exact fuzz run
#
# Portable POSIX shell: must behave identically under bash and under hellish.
# ---------------------------------------------------------------------------
cd "$(dirname "$0")" || exit 1
. ./config.sh
. ./lib/irc_lib.sh

report_init "13: command grammar (RFC 2812) + fuzz"

if ! command -v python3 >/dev/null 2>&1; then
    t_fail "python3 not available — the grammar suite cannot run"
    report_summary
    exit $?
fi

if [ ! -x "$BIN" ]; then
    t_fail "server binary not found at $BIN — build first"
    report_summary
    exit $?
fi

# --- per-command conformance ----------------------------------------------
CONF_LOG=$(mktemp /tmp/ftirc_grammar_conf.XXXXXX)
python3 ./grammar/conformance.py --binary "$BIN" --port "${GRAMMAR_PORT:-7500}" \
    >"$CONF_LOG" 2>&1
t_assert $? "every RFC 2812 command production behaves as the RFC says"
sed 's/^/      /' "$CONF_LOG" | tail -n 25

# Divergences are printed above rather than counted: they are places the RFC
# leaves open, and they must stay visible so a deliberate deviation gets
# re-read on every run rather than forgotten.
rm -f "$CONF_LOG"

# --- fuzz -----------------------------------------------------------------
FUZZ_LOG=$(mktemp /tmp/ftirc_grammar_fuzz.XXXXXX)
FUZZ_ARGS="--cases ${FUZZ_CASES:-600}"
[ -n "${FUZZ_SEED:-}" ] && FUZZ_ARGS="$FUZZ_ARGS --seed $FUZZ_SEED"
# shellcheck disable=SC2086
python3 ./grammar/fuzz.py --binary "$BIN" --port "${FUZZ_PORT:-7600}" $FUZZ_ARGS \
    >"$FUZZ_LOG" 2>&1
t_assert $? "mutated command syntax never breaks liveness or framing"
sed 's/^/      /' "$FUZZ_LOG" | tail -n 12
rm -f "$FUZZ_LOG"

report_summary
