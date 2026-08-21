#!/usr/bin/env bash
# Prints the resolved suite configuration and sanity-checks it.
# Portable POSIX shell: behaves identically under bash and hellish.
cd "$(dirname "$0")" || exit 1

if [ ! -f ./config.sh ]; then
    printf 'ERROR: config.sh not found next to %s\n' "$0" >&2
    exit 1
fi

. ./config.sh

printf '=== resolved configuration ===\n'
printf 'TEST_DIR      = %s\n' "$TEST_DIR"
printf 'PROJECT_DIR   = %s\n' "$PROJECT_DIR"
printf 'BIN           = %s\n' "$BIN"
printf 'IRC_HOST      = %s\n' "$IRC_HOST"
printf 'IRC_PORT      = %s\n' "$IRC_PORT"
printf 'IRC_PASSWORD  = %s\n' "$IRC_PASSWORD"
printf 'STARTUP_PORT  = %s\n' "$STARTUP_PORT"

printf '\n=== sanity ===\n'
rc=0

if [ -d "$PROJECT_DIR/src" ]; then
    printf 'OK   PROJECT_DIR contains src/\n'
else
    printf 'WARN PROJECT_DIR has no src/ — is it really the project root?\n'
    rc=1
fi

if [ -x "$BIN" ]; then
    printf 'OK   BIN exists and is executable\n'
else
    printf 'WARN BIN not built yet (run make)\n'
    rc=1
fi

if [ "$IRC_PORT" = "$STARTUP_PORT" ]; then
    printf 'FAIL IRC_PORT and STARTUP_PORT are both %s — they must differ,\n' "$IRC_PORT"
    printf '     01_startup.sh binds its own server and would collide.\n'
    rc=1
else
    printf 'OK   IRC_PORT and STARTUP_PORT differ\n'
fi

for v in IRC_HOST IRC_PORT IRC_PASSWORD; do
    eval "val=\$$v"
    if [ -z "$val" ]; then
        printf 'FAIL %s is empty\n' "$v"
        rc=1
    fi
done

# The values must survive being exported into a child process. config.sh
# exports one name per statement because `export A B C` is mis-parsed by some
# shells; this check is what catches a regression of that.
child=$(sh -c 'printf "%s|%s|%s" "$BIN" "$IRC_HOST" "$IRC_PORT"')
if [ "$child" = "$BIN|$IRC_HOST|$IRC_PORT" ]; then
    printf 'OK   exported values reach a child process intact\n'
else
    printf 'FAIL exported values are corrupted in a child process\n'
    printf '     parent: %s\n' "$BIN|$IRC_HOST|$IRC_PORT"
    printf '     child : %s\n' "$child"
    rc=1
fi

exit "$rc"
