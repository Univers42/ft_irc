#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# driver.sh scenario <file>   — replay a timed conversation, then exit
# driver.sh chatter  <roster> — generate small talk until killed
#
# Runs detached, in the background, for the life of the simulation.
# ---------------------------------------------------------------------------
set -uo pipefail

SIM_LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export SIM_LIB_DIR
# shellcheck disable=SC1090
. "$SIM_LIB_DIR/lib.sh"
sim_load_env || exit 1

mode="${1:-scenario}"
source_file="${2:-}"

case "$mode" in
scenario)
    [ -f "$source_file" ] || exit 1
    # No `read -r line < file` loop over a pipe here: the file is small and
    # read line-by-line so the delays stay relative to the previous line.
    while IFS= read -r raw; do
        # Skip blank and whole-line comments ONLY. An inline "${raw%%#*}"
        # strip would cut the line at its first '#', which in an IRC scenario
        # is the channel name: "PRIVMSG #general :hi" became "PRIVMSG " and
        # the server answered 411 :No recipient given.
        trimmed="${raw#"${raw%%[![:space:]]*}"}"
        case "$trimmed" in ''|'#'*) continue ;; esac
        line="$raw"
        delay="$(printf '%s' "$line" | awk '{print $1}')"
        nick="$(printf '%s' "$line" | awk '{print $2}')"
        # sed, not `cut -d' ' -f3-`: the scenario file is column-aligned with
        # runs of spaces, and cut treats every one of them as its own
        # delimiter — so field 3 came back as the NICK and the server saw the
        # nickname as a command (421 ALICE :Unknown command). This strips
        # exactly two whitespace-separated fields and leaves the rest of the
        # line byte-for-byte, so message spacing survives too.
        cmd="$(printf '%s' "$line" | sed -E 's/^[[:space:]]*[^[:space:]]+[[:space:]]+[^[:space:]]+[[:space:]]+//')"
        [ -n "$nick" ] && [ -n "$cmd" ] || continue
        sleep "$delay"
        sim_send "$nick" "$cmd" >/dev/null 2>&1
        printf '%(%H:%M:%S)T %s: %s\n' -1 "$nick" "$cmd"
    done < "$source_file"
    printf '%(%H:%M:%S)T scenario finished\n' -1
    ;;

chatter)
    LINES=(
        "any news?"
        "that worked, thanks"
        "i am seeing the same thing here"
        "give me five minutes"
        "pushed"
        "who is looking at this?"
        "works on my machine"
        "rebasing now"
        "can someone review?"
        "done"
    )
    # Only talk as clients that actually joined something — a lurker has no
    # channel to talk in, and PRIVMSG to a channel you are not in is 404.
    while :; do
        for nick in $(sim_client_list); do
            log="$(sim_client_dir "$nick")/raw.log"
            [ -f "$log" ] || continue
            chan="$(grep -ao ' JOIN [^ ]*' "$log" 2>/dev/null \
                    | awk '{print $2}' | tr -d ':\r' | sort -u | shuf -n1)"
            [ -n "$chan" ] || continue
            [ $(( RANDOM % 3 )) -eq 0 ] || continue
            sim_send "$nick" "PRIVMSG $chan :${LINES[$(( RANDOM % ${#LINES[@]} ))]}" \
                >/dev/null 2>&1
            printf '%(%H:%M:%S)T %s -> %s\n' -1 "$nick" "$chan"
            sleep 0.4
        done
        sleep 3
    done
    ;;
esac
