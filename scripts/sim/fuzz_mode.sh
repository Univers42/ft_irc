#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# fuzz_mode.sh — hammer MODE with every shape of mode string and check that
# what comes back is well formed.
#
# MODE is the widest parser surface in the server: a free-form sign/letter
# string plus a positional parameter list, where each letter decides for
# itself whether it consumes one. That combination is where undefined
# behaviour hides, so this drives it with hand-picked edge cases and random
# strings and checks six invariants on every reply.
#
#   I1  liveness      every case is followed by PING; the PONG must come back
#   I2  well formed   every reply <= 512 octets, no NUL, no embedded CR/LF
#   I3  no silence    a case naming a real mode letter must produce a
#                     broadcast or a numeric — never nothing at all
#   I4  sign sanity   a broadcast mode string must look like [+-]xx[+-]yy...
#                     with no doubled sign, no trailing sign, no empty run
#   I5  param count   the broadcast's parameter count must equal the number of
#                     param-taking letters it names
#   I6  round trip    the server must accept the mode string it just emitted;
#                     replaying a broadcast must not answer 461/472/525/696
#
# Usage: scripts/simulation.sh --fuzz-mode [cases]
# ---------------------------------------------------------------------------
set -uo pipefail

SIM_LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export SIM_LIB_DIR
# shellcheck disable=SC1090
. "$SIM_LIB_DIR/lib.sh"
sim_load_env || { err "no simulation running — start scripts/simulation.sh first"; exit 1; }

RANDOM_CASES="${1:-150}"
CHAN='#fuzz'
WORK="$SIM_DIR/fuzz"
rm -rf "$WORK"; mkdir -p "$WORK"

N_OK=0; N_BAD=0
FINDINGS="$WORK/findings.txt"
: > "$FINDINGS"

fail() {  # fail <invariant> <case> <detail>
    N_BAD=$((N_BAD+1))
    printf '%s[%s]%s %-34s %s\n' "$C_ERR" "$1" "$C_RESET" "«$2»" "$3"
    printf '%s\t%s\t%s\n' "$1" "$2" "$3" >> "$FINDINGS"
}

# --- two clients: an operator and a plain member ---------------------------

start_client() {  # start_client <name> <nick>
    _f="$WORK/$1.fifo"; rm -f "$_f"; mkfifo "$_f"
    nc -C "$IRC_HOST" "$IRC_PORT" < "$_f" > "$WORK/$1.log" 2>/dev/null &
    eval "PID_$1=\$!"
    sleep infinity > "$_f" 2>/dev/null &
    eval "HOLD_$1=\$!"
    sleep 0.2
    {
        printf 'PASS %s\r\n' "$IRC_PASSWORD"
        printf 'NICK %s\r\n' "$2"
        printf 'USER %s 0 * :%s\r\n' "$2" "$2"
        printf 'JOIN %s\r\n' "$CHAN"
    } >> "$_f"
    sleep 0.6
}
send() { printf '%s\r\n' "$2" >> "$WORK/$1.fifo"; }

stop_clients() {
    for _n in op mem; do
        eval "_h=\${HOLD_$_n:-}"; eval "_p=\${PID_$_n:-}"
        [ -n "${_h:-}" ] && kill "$_h" 2>/dev/null
        [ -n "${_p:-}" ] && kill "$_p" 2>/dev/null
    done
}
trap stop_clients EXIT

say "connecting fuzz clients to $CHAN on $IRC_HOST:$IRC_PORT"
start_client op  fuzzop      # joins first -> channel operator
start_client mem fuzzmem
grep -aq " 001 fuzzop"  "$WORK/op.log"  || { err "fuzzop never registered";  exit 1; }
grep -aq " 001 fuzzmem" "$WORK/mem.log" || { err "fuzzmem never registered"; exit 1; }
ok "fuzzop is operator, fuzzmem is a plain member"

# --- one case --------------------------------------------------------------

SEQ=0
run_case() {  # run_case <mode-string-and-params>
    SEQ=$((SEQ+1))
    _marker="fz$SEQ"
    _before=$(wc -c < "$WORK/op.log")

    send op "MODE $CHAN $1"
    send op "PING :$_marker"

    # I1 — wait for our own PONG. No marker means the server stopped talking.
    _n=0
    while [ "$_n" -lt 60 ]; do
        grep -aq "PONG.*$_marker" "$WORK/op.log" && break
        sleep 0.05; _n=$((_n+1))
    done
    if ! grep -aq "PONG.*$_marker" "$WORK/op.log"; then
        fail I1 "$1" "no PONG after the case — server stopped responding"
        return 1
    fi

    # Everything the server said about this case.
    tail -c "+$((_before+1))" "$WORK/op.log" | grep -av "PONG.*$_marker" > "$WORK/reply.txt"
    N_OK=$((N_OK+1))
    return 0
}

# --- invariant checks on the captured replies ------------------------------

check_replies() {  # check_replies <case>
    _case="$1"

    # I2 — well-formed lines.
    # A NUL cannot be written into a bash string, so $'\000' is the EMPTY
    # string and `grep -q ''` matches every line — which reported a NUL in
    # every single reply. Compare the byte count with and without NULs
    # instead; that needs no NUL-bearing shell variable at all.
    _raw_bytes=$(wc -c < "$WORK/reply.txt")
    _sans_nul=$(tr -d '\000' < "$WORK/reply.txt" | wc -c)
    if [ "$_raw_bytes" -ne "$_sans_nul" ]; then
        fail I2 "$_case" "reply contains $(( _raw_bytes - _sans_nul )) NUL byte(s)"
    fi
    while IFS= read -r line; do
        [ -n "$line" ] || continue
        _len=${#line}
        [ "$_len" -gt 512 ] && fail I2 "$_case" "reply line is $_len octets (>512)"
        case "$line" in
            :*) : ;;
            *)  fail I2 "$_case" "reply has no prefix: $line" ;;
        esac
    done < "$WORK/reply.txt"

    # The MODE broadcast for this case, if there was one.
    _bc="$(grep -a " MODE $CHAN " "$WORK/reply.txt" | tail -1)"
    _numerics="$(grep -aoE ' (4[0-9][0-9]|5[0-9][0-9]|6[0-9][0-9]) ' "$WORK/reply.txt" | tr -d ' ' | tr '\n' ',')"

    # I3 — a case naming a real mode letter must say something.
    _modefield="$(printf '%s' "$_case" | cut -d' ' -f1)"
    if printf '%s' "$_modefield" | grep -q '[itklo]'; then
        if [ -z "$_bc" ] && [ -z "$_numerics" ]; then
            fail I3 "$_case" "named a real mode letter but the server said nothing"
        fi
    fi

    [ -n "$_bc" ] || return 0

    # Broadcast shape: ":prefix MODE #fuzz <modestr> [params...]"
    _rest="${_bc#* MODE $CHAN }"
    _modestr="$(printf '%s' "$_rest" | awk '{print $1}' | tr -d '\r')"
    _params="$(printf '%s' "$_rest" | cut -d' ' -f2- | tr -d '\r')"
    [ "$_params" = "$_modestr" ] && _params=""

    # I4 — sign sanity.
    if ! printf '%s' "$_modestr" | grep -qE '^([+-][a-zA-Z]+)+$'; then
        fail I4 "$_case" "malformed broadcast mode string: '$_modestr'"
    fi

    # I5 — parameter count must match the param-taking letters named.
    # o always takes one; k takes one on +; l takes one on +.
    _want=0; _wantmax=0; _sign='+'
    _i=0
    while [ "$_i" -lt "${#_modestr}" ]; do
        _c="${_modestr:$_i:1}"
        case "$_c" in
            +|-) _sign="$_c" ;;
            o)   _want=$((_want+1)); _wantmax=$((${_wantmax:-0}+1)) ;;
            l)   [ "$_sign" = '+' ] && { _want=$((_want+1)); _wantmax=$((${_wantmax:-0}+1)); } ;;
            k)   if [ "$_sign" = '+' ]; then
                     _want=$((_want+1)); _wantmax=$((${_wantmax:-0}+1))
                 else
                     # -k's argument is optional: 0 or 1.
                     _wantmax=$((${_wantmax:-0}+1))
                 fi ;;
        esac
        _i=$((_i+1))
    done
    _wantmax=${_wantmax:-0}
    _got=0
    for _p in $_params; do _got=$((_got+1)); done
    if [ "$_got" -lt "$_want" ] || [ "$_got" -gt "$_wantmax" ]; then
        fail I5 "$_case" "broadcast '$_modestr $_params' expects $_want..$_wantmax param(s) but carries $_got"
    fi

    # I7 — no duplicate reply lines. One MODE naming the same unknown letter
    # twice must not answer twice: the reply is about the letter, and the
    # client learns nothing from the repeat.
    _dupes="$(sort "$WORK/reply.txt" | uniq -d | head -1)"
    if [ -n "$_dupes" ]; then
        _dupn=$(sort "$WORK/reply.txt" | uniq -d | wc -l)
        fail I7 "$_case" "$_dupn duplicated reply line(s), e.g. ${_dupes#* }"
    fi

    # I8 — bounded replies. A single MODE is one command; answering it with a
    # line per character turns a 512-octet request into tens of kilobytes of
    # output charged to the server's send queue.
    # The legitimate ceiling is one reply per DISTINCT complaint, so scale
    # the threshold to the distinct letters this case actually names (+2 for
    # the broadcast and a shared 461). A flat limit would either miss real
    # amplification on short strings or cry wolf on "+abcdefgh".
    _lines=$(grep -ac . "$WORK/reply.txt")
    _distinct=$(printf '%s' "$_case" | cut -d' ' -f1 | grep -o '[^+-]' | sort -u | wc -l)
    if [ "$_lines" -gt $(( _distinct + 2 )) ]; then
        fail I8 "$_case" "$_lines reply lines for $_distinct distinct mode letter(s) — amplification"
    fi

    # I9 — parameter theft. If the command ran out of parameters *and* the
    # broadcast echoes fewer than were supplied, some mode silently ate one
    # without reporting it, starving the modes behind it.
    if printf '%s' "$_numerics" | grep -q '461'; then
        _supplied=$(printf '%s' "$_case" | cut -s -d' ' -f2- | tr ' ' '\n' \
                    | grep -c . )
        _supplied_distinct=$(printf '%s' "$_case" | cut -s -d' ' -f2- | tr ' ' '\n' \
                    | grep . | sort -u | wc -l)
        # 441/525/696 each report a parameter that was consumed and rejected,
        # so they account for it just as the broadcast does. Only what
        # neither explains is genuinely eaten in silence.
        _accounted=$(grep -acE ' (441|525|696) ' "$WORK/reply.txt")
        if [ "$_supplied_distinct" -gt $(( _got + _accounted )) ]; then
            fail I9 "$_case" "461 raised, $_supplied_distinct distinct param(s) supplied, $_got echoed + $_accounted rejected — $(( _supplied_distinct - _got - _accounted )) consumed silently"
        fi
    fi

    # I6 — the server must accept what it just emitted.
    _before2=$(wc -c < "$WORK/op.log")
    send op "MODE $CHAN $_modestr $_params"
    send op "PING :rt$SEQ"
    _n=0
    while [ "$_n" -lt 60 ]; do
        grep -aq "PONG.*rt$SEQ" "$WORK/op.log" && break
        sleep 0.05; _n=$((_n+1))
    done
    _rt="$(tail -c "+$((_before2+1))" "$WORK/op.log" | grep -aoE ' (461|472|525|696) ' | tr -d ' ' | tr '\n' ',')"
    if [ -n "$_rt" ]; then
        fail I6 "$_case" "server rejected its own broadcast '$_modestr $_params' with $_rt"
    fi
    return 0
}

reset_channel() {
    send op "MODE $CHAN -i"
    send op "MODE $CHAN -t"
    send op "MODE $CHAN -l"
    send op "MODE $CHAN -k x"
    send op "MODE $CHAN +o fuzzmem"
    send op "MODE $CHAN -o fuzzmem"
    sleep 0.15
}

probe() {  # probe <case>
    run_case "$1" || return 1
    check_replies "$1"
    reset_channel
}

# --- hand-picked edge cases ------------------------------------------------

section() { printf '\n%s%s%s\n' "$C_B" "$1" "$C_RESET"; }

section "Edge cases — signs"
for c in '+' '-' '++' '--' '+-' '-+' '+++++' '+-+-+-' ; do probe "$c"; done

section "Edge cases — bare and repeated letters"
for c in 'i' 'o fuzzmem' '+i' '-i' '+ii' '+i+i' '+i-i' '-i-i' \
         '+o fuzzmem' '-o fuzzmem' '++o fuzzmem' '+++++o fuzzmem' \
         '+-o fuzzmem' '-+o fuzzmem' '+-+-o fuzzmem' \
         '+oo fuzzmem fuzzop' '+o+o fuzzmem fuzzop' '+o-o fuzzmem fuzzmem' ; do
    probe "$c"
done

section "Edge cases — missing and surplus parameters"
for c in '+o' '+k' '+l' '+ol' '+ko' '+lo' \
         '+o fuzzmem extra' '+k secret extra' '+l 5 extra' \
         '+o nosuchnick' '+o ""' ; do
    probe "$c"
done

section "Edge cases — parameterised modes"
for c in '+k secret' '-k' '-k secret' '-k+o fuzzmem' '+k-k secret' \
         '+l 5' '+l 0' '+l -1' '+l abc' '+l 99999999999999999999' '-l' \
         '+kl secret 5' '+lk 5 secret' '+ok fuzzmem secret' '+ko secret fuzzmem' ; do
    probe "$c"
done

section "Edge cases — unknown and hostile mode characters"
for c in '+z' '-z' '+Z' '+O' '+I' '+abc' 'jfsadfsahf' \
         '+o|o|+o-|*' '*' '|' '/' '\' '?' '.' ',' ':' ';' \
         '+*' '+?' '+/' '+i*t' '+o* fuzzmem' ; do
    probe "$c"
done

section "Edge cases — length"
probe "$(printf '+%.0si' $(seq 1 60))"
probe "$(printf 'o%.0s' $(seq 1 30)) fuzzmem"
probe "+$(printf 'z%.0s' $(seq 1 100))"

# --- random cases ----------------------------------------------------------

section "Random cases ($RANDOM_CASES)"
ALPHABET='+-+-itkloitkloabzZOIT*|?.'
for _r in $(seq 1 "$RANDOM_CASES"); do
    _len=$(( (RANDOM % 10) + 1 ))
    _s=''
    _j=0
    while [ "$_j" -lt "$_len" ]; do
        _idx=$(( RANDOM % ${#ALPHABET} ))
        _s="$_s${ALPHABET:$_idx:1}"
        _j=$((_j+1))
    done
    _np=$(( RANDOM % 3 ))
    _args=''
    _k=0
    while [ "$_k" -lt "$_np" ]; do
        case $(( RANDOM % 4 )) in
            0) _args="$_args fuzzmem" ;;
            1) _args="$_args 7" ;;
            2) _args="$_args key$_r" ;;
            3) _args="$_args nosuch" ;;
        esac
        _k=$((_k+1))
    done
    probe "${_s}${_args}"
done

# --- summary ---------------------------------------------------------------

printf '\n%s%s%s\n' "$C_B" "─────────────────────────────────────────────" "$C_RESET"
printf '  cases run: %d   violations: %s%d%s\n\n' "$N_OK" \
    "$([ "$N_BAD" -gt 0 ] && printf '%s' "$C_ERR" || printf '%s' "$C_OK")" \
    "$N_BAD" "$C_RESET"

if [ "$N_BAD" -gt 0 ]; then
    printf '  by invariant:\n'
    cut -f1 "$FINDINGS" | sort | uniq -c | sed 's/^/    /'
    printf '\n  full list: %s\n' "$FINDINGS"
    exit 1
fi
ok "no MODE invariant violations"
