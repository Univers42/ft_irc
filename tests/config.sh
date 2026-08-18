#!/usr/bin/env bash
: "${PROJECT_DIR:=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
: "${BIN:=$PROJECT_DIR/ircserv}"
: "${IRC_HOST:=127.0.0.1}"
: "${IRC_PORT:=6667}"
: "${IRC_PASSWORD:=pass}"
: "${STARTUP_PORT:=6668}"

export PROJECT_DIR BIN IRC_HOST IRC_PORT IRC_PASSWORD STARTUP_PORT
