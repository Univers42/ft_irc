#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# run_dual.sh — run the entire ft_irc shell suite twice, once under each
# shell, and diff the two transcripts.
#
#   ./run_dual.sh                       bash vs hellish (default)
#   ./run_dual.sh bash dash             any two shells
#   ./run_dual.sh --keep                keep the transcripts around
#
# Any difference is either a server nondeterminism or a shell bug. Timing
# noise is filtered out (elapsed seconds, pids, temp paths) so that what's
# left is signal.
#
# Exit status: 0 if the two transcripts agree, 1 if they differ.
# ---------------------------------------------------------------------------
cd "$(dirname "$0")" || exit 1

KEEP=0
SHELL_A=""
SHELL_B=""
PASSTHRU=""
for arg in "$@"; do
    case "$arg" in
        --keep) KEEP=1 ;;
        -*)     PASSTHRU="$PASSTHRU $arg" ;;   # forwarded to run_all.sh
        *)      if [ -z "$SHELL_A" ]; then SHELL_A="$arg"; else SHELL_B="$arg"; fi ;;
    esac
done
[ -z "$SHELL_A" ] && SHELL_A=bash
[ -z "$SHELL_B" ] && SHELL_B=hellish

for sh in "$SHELL_A" "$SHELL_B"; do
    if ! command -v "$sh" >/dev/null 2>&1; then
        printf 'FATAL: shell not found: %s\n' "$sh" >&2
        exit 2
    fi
done

OUT_A="/tmp/ftirc_dual_$(basename "$SHELL_A").txt"
OUT_B="/tmp/ftirc_dual_$(basename "$SHELL_B").txt"

# Strip the things that legitimately differ between two runs of anything:
# pids, elapsed times, $$-suffixed temp paths, and the SHELL= banner line.
normalize() {
    sed -e 's/pid [0-9][0-9]*/pid PID/g' \
        -e 's/(rc=[0-9][0-9]*)/(rc=RC)/g' \
        -e 's/in [0-9][0-9]*s/in Ns/g' \
        -e 's|/tmp/[A-Za-z_.]*\.[0-9][0-9]*|/tmp/TMP|g' \
        -e 's|ftirc_bash_[0-9][0-9]*|ftirc_TMP|g' \
        -e '/^# SHELL=/d' \
        -e 's/[0-9][0-9]*\.[0-9][0-9]* *seconds/N seconds/g' \
        "$1"
}

printf '=== run 1/2: %s ===\n' "$SHELL_A"
SHELL_UNDER_TEST="$SHELL_A" "$SHELL_A" ./run_all.sh $PASSTHRU > "$OUT_A" 2>&1
rc_a=$?
printf 'exit=%s  transcript=%s\n' "$rc_a" "$OUT_A"

printf '\n=== run 2/2: %s ===\n' "$SHELL_B"
SHELL_UNDER_TEST="$SHELL_B" "$SHELL_B" ./run_all.sh $PASSTHRU > "$OUT_B" 2>&1
rc_b=$?
printf 'exit=%s  transcript=%s\n' "$rc_b" "$OUT_B"

printf '\n############################################\n'
printf '# %s vs %s\n' "$SHELL_A" "$SHELL_B"
printf '############################################\n'

normalize "$OUT_A" > "$OUT_A.norm"
normalize "$OUT_B" > "$OUT_B.norm"

if diff -u "$OUT_A.norm" "$OUT_B.norm" > /tmp/ftirc_dual.diff 2>&1; then
    printf 'IDENTICAL — %s and %s produced the same suite transcript.\n' "$SHELL_A" "$SHELL_B"
    status=0
else
    printf 'DIFFERENT — the two shells disagree. Full diff: /tmp/ftirc_dual.diff\n\n'
    sed -n '1,120p' /tmp/ftirc_dual.diff
    printf '\nEach difference is either server nondeterminism or a bug in one of\n'
    printf 'the two shells. Re-run the specific script under both to confirm:\n'
    printf '  %s ./NN_name.sh\n  %s ./NN_name.sh\n' "$SHELL_A" "$SHELL_B"
    status=1
fi

if [ "$rc_a" -ne "$rc_b" ]; then
    printf '\nNOTE: exit statuses also differ (%s=%s, %s=%s)\n' \
        "$SHELL_A" "$rc_a" "$SHELL_B" "$rc_b"
    status=1
fi

if [ "$KEEP" -eq 0 ]; then
    rm -f "$OUT_A.norm" "$OUT_B.norm"
fi
exit "$status"
