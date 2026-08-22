#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# hexchat_profile.sh <cfgdir> <nick> <realname> <channels-csv> <host> <port> <pass>
#
# Generates a throwaway, fully isolated HexChat configuration directory that
# auto-connects to the simulated server, auto-joins its channels, logs every
# tab to disk, and loads the simctl addon so the simulation can push commands
# into the live connection.
#
# The magic numbers in servlist.conf, all verified against HexChat 2.16:
#   F=8   FLAG_AUTO_CONNECT, and *not* FLAG_USE_GLOBAL — so the I/U/R below
#         are used instead of the user's real identity.
#   L=7   LOGIN_PASS: send the server password with PASS. Without it HexChat
#         holds the password back for a services login that never happens,
#         and every client sits unregistered.
# ---------------------------------------------------------------------------
set -u

cfg="$1"; nick="$2"; real="$3"; chans="$4"; host="$5"; port="$6"; pass="$7"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

mkdir -p "$cfg/addons" "$cfg/logs"

cat > "$cfg/hexchat.conf" <<EOF
gui_slist_skip = 1
gui_join_dialog = 0
gui_quit_dialog = 0
gui_tray = 0
gui_lagometer = 0
gui_throttlemeter = 0
irc_logging = 1
irc_logmask = %n/%c.log
stamp_log = 1
stamp_log_format = %H:%M:%S 
irc_nick1 = $nick
irc_nick2 = ${nick}_
irc_nick3 = ${nick}__
irc_user_name = $nick
irc_real_name = $real
text_stripcolor_replay = 1
EOF

{
    printf 'v=2.16.0\n\n'
    printf 'N=ftircsim\n'
    printf 'I=%s\n' "$nick"
    printf 'i=%s_\n' "$nick"
    printf 'U=%s\n' "$nick"
    printf 'R=%s\n' "$real"
    printf 'P=%s\n' "$pass"
    printf 'L=7\n'
    printf 'E=UTF-8 (Unicode)\n'
    printf 'F=8\n'
    printf 'D=0\n'
    printf 'S=%s/%s\n' "$host" "$port"
    # ONE J= line per channel. A comma-separated "J=#a,#b,#c" is accepted by
    # the parser but HexChat 2.16 autojoins only the first entry — verified
    # against the server, not guessed.
    if [ "$chans" != "-" ] && [ -n "$chans" ]; then
        # printf '%s\n', not '%s': without the trailing newline `read` returns
        # non-zero on the final field and the loop body never runs for it, so
        # the LAST channel of every list silently went missing.
        printf '%s\n' "$chans" | tr ',' '\n' | while IFS= read -r one; do
            [ -n "$one" ] && printf 'J=%s\n' "$one"
        done
    fi
} > "$cfg/servlist.conf"

cp "$here/addon_simctl.py" "$cfg/addons/simctl.py"

rm -f "$cfg/ctl.fifo"
mkfifo "$cfg/ctl.fifo"
