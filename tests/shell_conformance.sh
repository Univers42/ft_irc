#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# shell_conformance.sh — differential shell prober.
#
# Runs a corpus of small snippets under two shells and reports every case
# where stdout or exit status disagree. This is how the hellish bugs filed
# against Univers42/hellish were found; keep adding cases as you find more.
#
#   ./shell_conformance.sh                  bash vs hellish
#   ./shell_conformance.sh bash dash        any two shells
#   ./shell_conformance.sh -v               also print the SAME cases
#
# Exit status: 0 if every case agrees, 1 otherwise.
#
# Each case is one line of the corpus below:  <name><TAB><snippet>
# Snippets must be single-line; use ';' rather than newlines.
# ---------------------------------------------------------------------------
cd "$(dirname "$0")" || exit 1

VERBOSE=0
SHELL_A=""
SHELL_B=""
for arg in "$@"; do
    case "$arg" in
        -v|--verbose) VERBOSE=1 ;;
        *) if [ -z "$SHELL_A" ]; then SHELL_A="$arg"; else SHELL_B="$arg"; fi ;;
    esac
done
[ -z "$SHELL_A" ] && SHELL_A=bash
[ -z "$SHELL_B" ] && SHELL_B=hellish

for sh in "$SHELL_A" "$SHELL_B"; do
    command -v "$sh" >/dev/null 2>&1 || {
        printf 'FATAL: shell not found: %s\n' "$sh" >&2; exit 2; }
done

WORK=$(mktemp -d /tmp/shconf.XXXXXX) || exit 1
cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT

TOTAL=0
DIFFS=0

# run_one <shell> <snippet> -> "rc=N|out=..."
run_one() {
    _sh="$1"
    _snip="$2"
    _d="$WORK/run"
    rm -rf "$_d"
    mkdir -p "$_d"
    _out=$(cd "$_d" && timeout 10 "$_sh" -c "$_snip" 2>"$WORK/err")
    _rc=$?
    printf 'rc=%s|out=%s' "$_rc" "$_out"
}

compare() {
    name="$1"
    snip="$2"
    TOTAL=$((TOTAL + 1))
    a=$(run_one "$SHELL_A" "$snip")
    err_a=$(cat "$WORK/err" 2>/dev/null)
    b=$(run_one "$SHELL_B" "$snip")
    err_b=$(cat "$WORK/err" 2>/dev/null)
    if [ "$a" = "$b" ]; then
        [ "$VERBOSE" -eq 1 ] && printf 'SAME  %-32s %s\n' "$name" "$a"
        return 0
    fi
    DIFFS=$((DIFFS + 1))
    printf 'DIFF  %s\n' "$name"
    printf '        snippet : %s\n' "$snip"
    printf '        %-8s: %s\n' "$SHELL_A" "$a"
    [ -n "$err_a" ] && printf '        %-8s  stderr: %s\n' "$SHELL_A" "$err_a"
    printf '        %-8s: %s\n' "$SHELL_B" "$b"
    [ -n "$err_b" ] && printf '        %-8s  stderr: %s\n' "$SHELL_B" "$err_b"
    return 1
}

printf '############################################\n'
printf '# shell conformance: %s vs %s\n' "$SHELL_A" "$SHELL_B"
printf '############################################\n'

# --- the corpus -----------------------------------------------------------
# Read as <name><TAB><snippet>. Keep entries grouped by feature area.
while IFS='	' read -r cname csnip; do
    case "$cname" in ''|'#'*) continue ;; esac
    compare "$cname" "$csnip"
done <<'CORPUS'
expansion.brace.literal	echo "{a,b}"
expansion.brace.singlequoted	echo '{a,b}'
expansion.brace.variable	v='{a,b}'; echo "$v"
expansion.brace.cmdsub.dq	echo "$(echo '{a,b}')"
expansion.brace.cmdsub.bare	echo $(echo '{a,b}')
expansion.brace.cmdsub.backtick	echo "`echo '{a,b}'`"
expansion.brace.cmdsub.assign	v="$(echo '{a,b}')"; echo "$v"
expansion.brace.cmdsub.sideeffect	rm -f c; echo "$(printf 'r\n' >> c; echo '{a,b}')" >/dev/null; wc -l < c | tr -d ' '
expansion.brace.cmdsub.awk	echo "[$(awk 'BEGIN{ printf "%d", 20 }')]"
expansion.brace.fromfile	printf '{a,b}\n' > bf; echo "$(cat bf)"
expansion.param.length	s=hello; echo ${#s}
expansion.param.default	echo ${nope:-fb}
expansion.param.assign	: "${z:=zz}"; echo "$z"
expansion.param.altvalue	x=1; echo ${x:+set}
expansion.param.stripsuffix	s=a.txt; echo ${s%.txt}
expansion.param.stripprefix	s=a.txt; echo ${s#a}
expansion.param.stripgreedy	s=a/b/c; echo ${s##*/}
expansion.tilde	cd /tmp; echo ~ | grep -c /
expansion.glob	touch ga gb; echo g[ab]
expansion.glob.nomatch	echo nosuchglob*
expansion.arith	echo $((2 + 3 * 4))
expansion.arith.nested	a=2; echo $(( (a + 1) * 3 ))
builtin.export.single	A=1; export A; sh -c 'echo $A'
builtin.export.multi	A=1; B=2; C=3; export A B C; echo "$A $B $C"
builtin.export.multi.env	A=1; B=2; export A B; sh -c 'echo "[$A][$B]"'
builtin.export.assignform	export A=1 B=2; echo "$A $B"
builtin.export.bare	export FOO BAR; echo "[$FOO][$BAR]"
builtin.shift	set -- a b c; shift; echo "$*"
builtin.read	printf 'v\n' | { read x; echo "$x"; }
builtin.read.ifs	printf 'a:b\n' | { IFS=: read x y; echo "$x-$y"; }
builtin.local	f() { local v=in; echo "$v"; }; f; echo "[${v-unset}]"
builtin.return	f() { return 4; }; f; echo $?
builtin.trap.exit	trap 'echo bye' EXIT; echo main
builtin.eval.dynvar	n=foo; eval "V_$n=7"; eval "echo \$V_$n"
builtin.command_v	command -v echo >/dev/null && echo found
quoting.singlequote	echo 'a  b'
quoting.doublequote	echo "a  b"
quoting.mixed	echo "it's"
quoting.backslash	echo a\ b
quoting.dollar.literal	echo '$HOME'
redir.out	echo x > f; cat f
redir.append	echo a > f; echo b >> f; wc -l < f | tr -d ' '
redir.err	ls /nonexistent 2>/dev/null; echo done
redir.fd.exec	exec 7> f; echo hi >&7; exec 7>&-; cat f
redir.fd.variable	exec 8> f; d=8; echo hi >&"$d"; exec 8>&-; cat f
redir.fd.readwrite	exec 9<> f; echo data >&9; exec 9>&-; cat f
control.if	if [ 1 -eq 1 ]; then echo y; else echo n; fi
control.while	i=0; while [ $i -lt 3 ]; do printf %s $i; i=$((i+1)); done
control.for	for x in a b c; do printf %s $x; done
control.case	case abc in a*) echo m;; *) echo n;; esac
control.andor	true && echo t || echo f
control.pipeline	echo hi | tr a-z A-Z
control.subshell	(cd /tmp; pwd)
control.exitstatus	(exit 3); echo $?
special.star	set -- a b c; echo "$*"
special.at	set -- a b c; for i in "$@"; do printf %s "$i"; done
special.hash	set -- a b c; echo $#
special.bang.waitstatus	sh -c 'exit 7' & wait $!; echo $?
special.bang.identity	sleep 19 & p=$!; c=$(ps -o comm= -p $p 2>/dev/null | tr -d ' '); kill -9 $p 2>/dev/null; echo "comm=$c"
special.bang.killreaches	sleep 18 & kill -9 $! 2>/dev/null; sleep 0.5; pgrep -x -f 'sleep 18' >/dev/null && echo ORPHANED || echo reaped
special.bang.stopreaches	sleep 17 & kill -STOP $! 2>/dev/null; sleep 0.4; ps -o stat= -p $(pgrep -x -f 'sleep 17' | head -1) 2>/dev/null | tr -d ' '; pkill -x sleep >/dev/null 2>&1
special.dollar0.cflag	echo "[$0]"
special.dollar0.operand	true
opts.set_u	set -u; echo "${nope}" 2>/dev/null; echo "rc=$?"
opts.set_e	set -e; false; echo reached
opts.pipefail	set -o pipefail 2>/dev/null; false | true; echo $?
CORPUS

printf '\n############################################\n'
if [ "$DIFFS" -eq 0 ]; then
    printf '# %d cases, 0 differences — the two shells agree.\n' "$TOTAL"
    printf '############################################\n'
    exit 0
fi
printf '# %d cases, %d DIFFERENCES\n' "$TOTAL" "$DIFFS"
printf '############################################\n'
printf '\nEach difference is a candidate bug in one of the two shells.\n'
printf 'Confirm by hand before reporting:  %s -c "<snippet>"\n' "$SHELL_B"
exit 1
