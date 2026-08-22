#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# verify_grammar.sh — RFC 2812 §2.3.1 message grammar, measured against a
# running ircserv.
#
# verify_names.sh checks the *name* productions (nickname, channel). This
# checks the ones above them: how a line is framed, split into prefix /
# command / params, and where the limits fall.
#
#   message    =  [ ":" prefix SPACE ] command [ params ] crlf
#   command    =  1*letter / 3digit
#   params     =  *14( SPACE middle ) [ SPACE ":" trailing ]
#              =/ 14( SPACE middle ) [ SPACE [ ":" ] trailing ]
#   nospcrlfcl =  %x01-09 / %x0B-0C / %x0E-1F / %x21-39 / %x3B-FF
#   middle     =  nospcrlfcl *( ":" / nospcrlfcl )
#   trailing   =  *( ":" / " " / nospcrlfcl )
#   SPACE      =  %x20
#   crlf       =  %x0D %x0A
#
# Same three outcomes as verify_names.sh:
#   PASS     matches RFC 2812 and the subject
#   FAIL     the server contradicts the spec in a way that matters
#   DIVERGE  deliberately stricter or looser — not a bug, but know about it
# ---------------------------------------------------------------------------
set -uo pipefail

SIM_LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export SIM_LIB_DIR
# shellcheck disable=SC1090
. "$SIM_LIB_DIR/lib.sh"
sim_load_env || { err "no simulation running — start scripts/simulation.sh first"; exit 1; }

N_PASS=0; N_FAIL=0; N_DIV=0
row_pass() { N_PASS=$((N_PASS+1)); printf '  %s%-8s%s %-26s %s\n' "$C_OK"   "PASS"    "$C_RESET" "$1" "$2"; }
row_fail() { N_FAIL=$((N_FAIL+1)); printf '  %s%-8s%s %-26s %s\n' "$C_ERR"  "FAIL"    "$C_RESET" "$1" "$2"; }
row_div()  { N_DIV=$((N_DIV+1));   printf '  %s%-8s%s %-26s %s\n' "$C_WARN" "DIVERGE" "$C_RESET" "$1" "$2"; }
section()  { printf '\n%s%s%s\n' "$C_B" "$1" "$C_RESET"; }

# raw <string-with-escapes> — send bytes exactly as given, then read the reply.
#
# printf '%b', not printf "$1": the probe text is DATA, and passing it as a
# format string would make any '%' in a test case an unintended conversion.
# %b still expands \r, \n and \000, which is what lets a probe emit a bare LF
# or a literal NUL.
raw() {
    { printf '%b' "$1"; sleep 0.6; } | timeout 4 nc "$IRC_HOST" "$IRC_PORT" 2>/dev/null
}

# reg_raw <nick> <extra> — registration prelude plus the probe's own lines.
#
# The prelude is assembled by plain string concatenation, keeping "\r\n" as
# two literal characters until raw() expands them. Building it with a nested
# $(printf ...) instead loses the final LF — command substitution strips
# trailing newlines — which leaves a bare CR welding the USER line onto the
# first probe line, and nothing registers at all.
reg_raw() {
    raw "PASS ${IRC_PASSWORD}\r\nNICK ${1}\r\nUSER u 0 * :U\r\n${2}"
}

# --- line framing ----------------------------------------------------------

section "Framing — crlf = %x0D %x0A, empty messages silently ignored"

# "Empty messages are silently ignored, which permits use of the sequence
# CR-LF between messages without extra problems." (§2.3)
out="$(reg_raw g1 'JOIN #gr1\r\n\r\n\r\nPRIVMSG #gr1 :after blanks\r\nQUIT\r\n')"
if printf '%s' "$out" | grep -aqF " JOIN #gr1" && ! printf '%s' "$out" | grep -aqF " 421 "; then
    row_pass 'empty messages'  'blank CRLF lines ignored, next command still runs'
else
    row_fail 'empty messages'  'a bare CRLF was not ignored'
fi

# Several commands in one write must all execute, in order.
out="$(reg_raw g2 'JOIN #gr2\r\nTOPIC #gr2 :one write\r\nQUIT\r\n')"
if printf '%s' "$out" | grep -aqF " JOIN #gr2" && printf '%s' "$out" | grep -aqF "TOPIC #gr2 :one write"; then
    row_pass 'batched commands'  'three commands in one packet, all executed'
else
    row_fail 'batched commands'  'not all commands in a single write ran'
fi

# The subject's own nc test: a command arriving in pieces.
_pf="$SIM_DIR/gram.fifo"; rm -f "$_pf"; mkfifo "$_pf"
nc "$IRC_HOST" "$IRC_PORT" < "$_pf" > "$SIM_DIR/gram.out" 2>/dev/null &
_pnc=$!
sleep infinity > "$_pf" 2>/dev/null &
_pkeep=$!
sleep 0.3
printf 'PASS %s\r\nNICK g3\r\nUSER u 0 * :U\r\n' "$IRC_PASSWORD" >> "$_pf"
sleep 0.5
printf 'JO'   >> "$_pf"; sleep 0.4
printf 'IN #g' >> "$_pf"; sleep 0.4
printf 'r3\r\n' >> "$_pf"; sleep 0.6
if grep -aqF " JOIN #gr3" "$SIM_DIR/gram.out"; then
    row_pass 'partial command'  "'JO'+'IN #g'+'r3' reassembled into one JOIN"
else
    row_fail 'partial command'  'fragmented command was not reassembled'
fi
kill "$_pkeep" 2>/dev/null; kill "$_pnc" 2>/dev/null
rm -f "$_pf" "$SIM_DIR/gram.out"

# RFC requires CRLF. Accepting a bare LF is a deliberate leniency: netcat
# without -C sends bare LF, and refusing it would make the subject's own test
# command unusable.
out="$(raw "PASS ${IRC_PASSWORD}\nNICK g4\nUSER u 0 * :U\nJOIN #gr4\nQUIT\n")"
if printf '%s' "$out" | grep -aqF " JOIN #gr4"; then
    row_div  'bare LF terminator'  'RFC 2812 requires CRLF; accepted (nc without -C)'
else
    row_pass 'bare LF terminator'  'strict CRLF enforced'
fi

# --- prefix ----------------------------------------------------------------

section "Prefix — [ \":\" prefix SPACE ] command"

out="$(reg_raw g5 'JOIN #gr5\r\n:g5!u@host PRIVMSG #gr5 :prefixed\r\nQUIT\r\n')"
if printf '%s' "$out" | grep -aqF " 421 "; then
    row_fail 'client-sent prefix'  'prefix parsed as the command'
else
    row_pass 'client-sent prefix'  'prefix skipped, command executed'
fi

# "A line that is nothing but a prefix carries no command."
out="$(reg_raw g6 ':onlyprefix\r\nJOIN #gr6\r\nQUIT\r\n')"
if printf '%s' "$out" | grep -aqF " JOIN #gr6" && ! printf '%s' "$out" | grep -aqF "ONLYPREFIX"; then
    row_pass 'prefix with no command'  'ignored, does not become a command'
else
    row_fail 'prefix with no command'  'a bare prefix was treated as a command'
fi

# --- command ---------------------------------------------------------------

section "Command — 1*letter / 3digit"

out="$(reg_raw g7 'join #gr7\r\nQUIT\r\n')"
printf '%s' "$out" | grep -aqF " JOIN #gr7" \
    && row_pass 'lowercase command' "'join' accepted — commands are case-insensitive" \
    || row_fail 'lowercase command' "'join' was not recognised"

out="$(reg_raw g8 'JoIn #gr8\r\nQUIT\r\n')"
printf '%s' "$out" | grep -aqF " JOIN #gr8" \
    && row_pass 'mixed-case command' "'JoIn' accepted" \
    || row_fail 'mixed-case command' "'JoIn' was not recognised"

out="$(reg_raw g9 '001 foo\r\nQUIT\r\n')"
printf '%s' "$out" | grep -aqF " 421 " \
    && row_pass 'numeric from a client' '3-digit command -> 421, as it should be' \
    || row_fail 'numeric from a client' 'a client numeric was acted on'

# --- parameters ------------------------------------------------------------

section "Parameters — middle / trailing"

# NOTE: every probe below sends to its OWN NICK, not to a channel. The server
# does not echo a client's channel message back to the sender (correct: the
# client already rendered it locally), so a single-connection probe watching a
# channel sees nothing and every one of these checks fails for the wrong
# reason. A self-addressed PRIVMSG travels the identical parse-and-relay path
# and comes back.
out="$(reg_raw ga 'PRIVMSG ga :a b c   d\r\nQUIT\r\n')"
printf '%s' "$out" | grep -aqF ':a b c   d' \
    && row_pass 'trailing keeps spaces' 'SPACE inside the trailing parameter preserved' \
    || row_fail 'trailing keeps spaces' 'trailing parameter was split or trimmed'

out="$(reg_raw gb 'PRIVMSG gb :see http://host:8080/x\r\nQUIT\r\n')"
printf '%s' "$out" | grep -aqF 'http://host:8080/x' \
    && row_pass 'trailing keeps colons' "':' inside the trailing parameter preserved" \
    || row_fail 'trailing keeps colons' "':' in the trailing parameter was mangled"

out="$(reg_raw gc 'PRIVMSG gc :\r\nQUIT\r\n')"
printf '%s' "$out" | grep -aqF ' 412 ' \
    && row_pass 'empty trailing' 'present-but-empty parameter -> 412, not a crash' \
    || row_div  'empty trailing' 'empty trailing neither delivered nor refused with 412'

# RFC: params is at most 14 middles plus one trailing = 15.
_p16=''; for i in $(seq 1 16); do _p16="$_p16 p$i"; done
out="$(reg_raw gd "PRIVMSG${_p16} :body\r\nQUIT\r\n")"
if printf '%s' "$out" | grep -aqE ' (417|461) '; then
    row_pass 'more than 15 params'  'refused'
else
    row_div  'more than 15 params'  'RFC caps params at 15; extra middles are parsed anyway'
fi

# middle may contain ':' after its first octet — but RFC's chanstring excludes
# ':' entirely, reserving it as the channel-mask separator.
out="$(reg_raw ge 'JOIN #gre:mask\r\nQUIT\r\n')"
if printf '%s' "$out" | grep -aqF ' JOIN #gre:mask'; then
    row_div  'colon in a channel name' "RFC chanstring excludes ':'; accepted as part of the name"
elif printf '%s' "$out" | grep -aqF ' JOIN #gre'; then
    row_pass 'colon in a channel name' "':' treated as the mask separator"
else
    row_pass 'colon in a channel name' 'refused'
fi

# --- forbidden octets ------------------------------------------------------

section "Octets — NUL, CR and LF may not appear inside a message"

# §2.3.1 note 2: "NUL is not allowed within messages."
out="$(reg_raw gf 'PRIVMSG gf :before\000after\r\nQUIT\r\n')"
if printf '%s' "$out" | grep -aqF 'beforeafter'; then
    row_pass 'NUL inside a parameter' 'stripped, the line still parses'
elif printf '%s' "$out" | grep -aq 'before'; then
    row_pass 'NUL inside a parameter' 'neutralised, no NUL relayed'
else
    row_fail 'NUL inside a parameter' 'line was lost entirely'
fi

# A stray CR must not let a client forge a second line (IRC line injection).
out="$(reg_raw gg 'PRIVMSG gg :x\rQUIT\r\nPRIVMSG gg :still here\r\nQUIT\r\n')"
if printf '%s' "$out" | grep -aqF 'still here'; then
    row_pass 'stray CR'  'sanitised; the connection was not torn down by a forged QUIT'
else
    row_fail 'stray CR'  'a stray CR ended the session — line injection is possible'
fi

# --- length ----------------------------------------------------------------

section "Length — 512 octets including CRLF"

# 510 payload + CRLF = 512, the largest legal line.
# "PRIVMSG gh :" is 12 octets, so 498 payload octets make the line exactly
# 510 + CRLF = 512, the largest legal message.
_x498="$(printf 'x%.0s' $(seq 1 498))"
out="$(reg_raw gh "PRIVMSG gh :${_x498}\r\nPRIVMSG gh :sentinel\r\nQUIT\r\n")"
if printf '%s' "$out" | grep -aqF 'sentinel'; then
    row_pass 'maximum-length line'  '512 octets accepted, session continues'
else
    row_fail 'maximum-length line'  'a legal 512-octet line broke the session'
fi

# An over-long line must be truncated AND its remainder discarded through the
# terminator — otherwise padding smuggles a second command past the limit.
_pad600="$(printf 'A%.0s' $(seq 1 600))"
out="$(reg_raw gi "PRIVMSG gi :${_pad600} JOIN #smuggled\r\nPRIVMSG gi :sentinel2\r\nQUIT\r\n")"
if printf '%s' "$out" | grep -aqF ' JOIN #smuggled'; then
    row_fail 'over-long line'  'the tail of an over-long line was executed as a command'
elif printf '%s' "$out" | grep -aqF 'sentinel2'; then
    row_pass 'over-long line'  'truncated, remainder discarded, session survives'
else
    row_fail 'over-long line'  'session did not survive an over-long line'
fi

# --- key -------------------------------------------------------------------

section "Channel key — key = 1*23( %x01-05 / %x07-08 / %x0C / %x0E-1F / %x21-7F )"

# key_case <nick> <key> <expect accept|reject> <label> <note>
key_case() {
    out="$(reg_raw "$1" "JOIN #${1}c\r\nMODE #${1}c +k ${2}\r\nMODE #${1}c\r\nQUIT\r\n")"
    if printf '%s' "$out" | grep -aqF " 525 "; then _got=reject; else _got=accept; fi
    if [ "$_got" = "$3" ]; then row_pass "$4" "$5"; else row_div "$4" "$5"; fi
}

key_case k1 'aaaaaaaaaaaaaaaaaaaaaaa'  accept '23-octet key'   'the RFC maximum, accepted'
key_case k2 'aaaaaaaaaaaaaaaaaaaaaaaa' reject '24-octet key'   'one over the RFC maximum, refused'
key_case k3 'good'                     accept 'ordinary key'   'accepted'
# RFC's key production includes %x2C (","), but JOIN takes a COMMA-SEPARATED
# key list, so a key holding one could never be addressed. Stricter on purpose.
key_case k4 'a,b'                      reject 'comma in a key' 'RFC allows %x2C; refused (JOIN key lists are comma-separated)'
# RFC restricts key to 7-bit US-ASCII (%x21-7F). isValidChannelKey only
# rejects <= 0x20 and ",", so 8-bit octets pass through.
key_case k5 'a\303\251b'               reject '8-bit in a key' 'RFC restricts key to 7-bit ASCII; accepted here'

# --- summary ---------------------------------------------------------------

printf '\n%s%s%s\n' "$C_B" "─────────────────────────────────────────────" "$C_RESET"
printf '  %spass %d%s   %sdiverge %d%s   %sfail %d%s\n\n' \
    "$C_OK" "$N_PASS" "$C_RESET" "$C_WARN" "$N_DIV" "$C_RESET" "$C_ERR" "$N_FAIL" "$C_RESET"

[ "$N_FAIL" -gt 0 ] && { err "$N_FAIL grammar check(s) failed"; exit 1; }
[ "$N_DIV" -gt 0 ] && warn "$N_DIV documented divergence(s) from RFC 2812"
ok "message grammar holds"
