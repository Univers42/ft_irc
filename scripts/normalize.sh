#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
#  normalize.sh — verify / apply the ft_irc source style.
#
#  The ENFORCED gate is safe whitespace only: no trailing whitespace, a final
#  newline on every file. These are unambiguous and the whole tree satisfies
#  them.
#
#  clang-format is an ADVISORY baseline for new code, NOT a gate. clang-format
#  18 cannot reproduce two deliberate house conventions:
#    1. the space after a top-level '#'  ("# define" / "# include")
#    2. manual column alignment of one-liner declarations and wrapped
#       expression continuations (e.g. the Client.cpp getter block).
#  So a clang-format diff does not mean a file is wrong — it is reported for
#  information and never fails the check or rewrites the hand-aligned code.
#  (See the header of .clang-format for the details.)
#
#  Modes:
#    (no args)        apply safe whitespace fixes in place; report advisories.
#    --check          CI mode: fail ONLY on a whitespace violation; advisories
#                     are printed but never fail. Rewrites nothing.
#    --clang-format   opt-in: additionally run `clang-format -i` (will collapse
#                     the manual alignment / "# define" spacing — for new files).
# ─────────────────────────────────────────────────────────────────────────────
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

CHECK=0
RUN_CLANG=0
case "${1:-}" in
	--check)        CHECK=1 ;;
	--clang-format) RUN_CLANG=1 ;;
esac

if [ -t 1 ]; then G=$'\033[1;32m'; R=$'\033[1;31m'; B=$'\033[1;34m'; Z=$'\033[0m'
else G=""; R=""; B=""; Z=""; fi

mapfile -t FILES < <(find src include -type f \( -name '*.cpp' -o -name '*.hpp' \
	-o -name '*.tpp' -o -name '*.ipp' \) 2>/dev/null)

# ── 1. ENFORCED: safe whitespace (trailing space, final newline) ────────────
printf "${B}== whitespace (enforced) ==${Z}\n"
ws_rc=0
for f in "${FILES[@]}"; do
	bad=""
	grep -nE ' +$' "$f" >/dev/null 2>&1 && bad="trailing-whitespace"
	# non-empty last byte that is not a newline → missing final newline
	if [ -s "$f" ] && [ -n "$(tail -c1 "$f")" ]; then
		bad="${bad:+$bad, }no-final-newline"
	fi
	if [ -n "$bad" ]; then
		echo "  ${R}$f${Z}: $bad"; ws_rc=1
	fi
done

if [ "$ws_rc" -eq 0 ]; then
	printf "${G}whitespace clean${Z}\n"
elif [ "$CHECK" -eq 0 ]; then
	# apply mode: fix whitespace in place via cpp_fmt.py when available
	CPPFMT="vendor/libcpp/vendor/scripts/cpp_fmt.py"
	if [ -f "$CPPFMT" ]; then
		python3 "$CPPFMT" "${FILES[@]}" >/dev/null 2>&1 && \
			printf "${G}whitespace fixed${Z}\n" && ws_rc=0
	fi
fi

# ── 2. ADVISORY: clang-format baseline diff (never fails, never auto-applied)
if command -v clang-format >/dev/null 2>&1; then
	printf "\n${B}== clang-format (advisory) ==${Z}\n"
	adv=0
	for f in "${FILES[@]}"; do
		diff -q "$f" <(clang-format "$f") >/dev/null 2>&1 || { echo "  differs: $f"; adv=1; }
	done
	if [ "$adv" -eq 0 ]; then
		printf "${G}matches clang-format baseline${Z}\n"
	else
		printf "  ${B}note:${Z} differences are expected — clang-format cannot express the\n"
		printf "  \"# define\" spacing or the manual column alignment. Advisory only.\n"
		if [ "$RUN_CLANG" -eq 1 ]; then
			clang-format -i "${FILES[@]}"
			printf "${R}clang-format applied (--clang-format): review the alignment churn${Z}\n"
		fi
	fi
fi

# ── 3. header include cycles (non-destructive report) ───────────────────────
CYC="vendor/libcpp/vendor/scripts/check_header_cycles.py"
if [ -f "$CYC" ]; then
	printf "\n${B}== header cycle report ==${Z}\n"
	python3 "$CYC" include src 2>&1 | sed 's/^/  /' || true
fi

# Exit status reflects ONLY the enforced whitespace gate.
exit "$ws_rc"
