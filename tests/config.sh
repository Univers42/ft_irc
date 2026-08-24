#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# config.sh — central config for the ft_irc shell test suite.
#
# Sourced by every test *after* it has cd'd into this directory, which is how
# we locate the project without ${BASH_SOURCE[0]}: that array is a bashism and
# this suite has to run byte-identically under bash and under hellish.
#
# Every value is overridable from the environment.
# ---------------------------------------------------------------------------

: "${TEST_DIR:=$(pwd)}"
: "${PROJECT_DIR:=$(cd "$TEST_DIR/.." && pwd)}"
# build/bin/ircserv is the only place the link step writes the binary; there
# is no ./ircserv symlink in the repo root any more.
: "${BIN:=$PROJECT_DIR/build/bin/ircserv}"
: "${IRC_HOST:=127.0.0.1}"
: "${IRC_PORT:=6667}"
: "${IRC_PASSWORD:=pass}"
: "${STARTUP_PORT:=6668}"    # used only by 01_startup.sh, so it never fights
                             # with the long-lived server run_all.sh starts

# NOTE: one `export` per name on purpose. `export A B C` is mis-parsed by
# hellish (it consumes the next name as a value, giving A=B and leaving B
# unexported) — see the hellish issue filed from tests/shell_conformance.sh.
export TEST_DIR
export PROJECT_DIR
export BIN
export IRC_HOST
export IRC_PORT
export IRC_PASSWORD
export STARTUP_PORT
