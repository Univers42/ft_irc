#!/usr/bin/env bash
# Central config for the ft_irc test suite. Edit these to match your setup,
# or override any of them as environment variables before running.

: "${PROJECT_DIR:=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/../..}"  # where your Makefile lives
: "${BIN:=$PROJECT_DIR/ircserv}"                                        # path to the built binary
: "${IRC_HOST:=127.0.0.1}"
: "${IRC_PORT:=6667}"
: "${IRC_PASSWORD:=pass}"
: "${STARTUP_PORT:=6668}"   # separate port used only by 01_startup.sh, so it
                             # doesn't fight with the long-lived server run_all.sh starts

export PROJECT_DIR BIN IRC_HOST IRC_PORT IRC_PASSWORD STARTUP_PORT
