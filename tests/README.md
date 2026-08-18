# ft_irc shell test suite

Everything here is **POSIX shell**. There is no Python anywhere in the suite,
and every script is written to behave identically under `bash` and under
[`hellish`](https://github.com/Univers42/hellish) so the two can be diffed
against each other.

## Quick start

```bash
make                       # from the project root — builds ./ircserv
cd tests
./check_config.sh          # show the resolved config and sanity-check it
bash ./run_all.sh          # run the whole suite
```

Useful flags:

```bash
bash ./run_all.sh --skip-build        # skip 12_build_norm.sh (it runs `make re`)
bash ./run_all.sh --with-valgrind     # add the valgrind pass
bash ./run_all.sh --only 05           # just the scripts whose name matches "05"
```

## Running it under two shells at once

`run_dual.sh` runs the entire suite twice and diffs the transcripts, filtering
out the things that legitimately differ between any two runs (pids, elapsed
seconds, temp paths):

```bash
./run_dual.sh                  # bash vs hellish
./run_dual.sh bash dash        # any two shells
```

Every remaining difference is either server nondeterminism or a bug in one of
the two shells.

`shell_conformance.sh` is the narrower tool: a corpus of ~66 one-line snippets
run under both shells, reporting each disagreement with its exact output. It is
how the hellish bugs listed at the bottom of this file were found.

```bash
./shell_conformance.sh         # bash vs hellish
./shell_conformance.sh -v      # also print the cases that agree
```

## Layout

| File | What it covers |
|---|---|
| `config.sh` | Ports, password, paths. Override any of it from the environment. |
| `lib/irc_lib.sh` | The client harness. Every client is a backgrounded `nc` fed by a FIFO held open on a dedicated fd, which is what lets a command be sent in pieces. |
| `00_shell_probe.sh` | Gate: does the running shell support what `irc_lib.sh` needs? Run it under two shells and diff. |
| `01_startup.sh` | `./ircserv` argv handling — bad ports, missing args, double bind. |
| `02_registration.sh` | PASS/NICK/USER, wrong password, duplicate nick, re-registration. |
| `03_tcp_framing.sh` | One byte at a time, several commands per packet, partial trailing command, bare LF, 20 KB lines, garbage bytes. |
| `04_disconnect.sh` | `kill -9` on the client — no QUIT — and the state it must free. |
| `05_privmsg.sh` | PRIVMSG/NOTICE routing, error replies, channel fan-out, case-insensitive nicks. |
| `06_channel_join_part.sh` | JOIN/PART semantics. |
| `07_kick_invite_topic.sh` | The operator-privilege surface. |
| `08_modes.sh` | `+i +t +k +o +l`. |
| `09_malformed_preauth.sh` | 451 gating before registration, then the malformed-input barrage. |
| `10_stress_multiclient.sh` | Many clients at once, and a frozen client that must not stall the others. |
| `11_memory_checks.sh` | Optional valgrind pass. |
| `12_build_norm.sh` | Builds every target, checks `-Wall -Wextra -Werror`, C++98, forbidden calls, exactly one poll call site, `fcntl` form. |
| `tools/mute_client.sh` | A client that registers and then never writes again — `--silent` to time the idle disconnect, `--freeze` to stop draining the socket. |

## Writing tests

Source the config and the library after `cd`ing into this directory:

```sh
cd "$(dirname "$0")" || exit 1
. ./config.sh
. ./lib/irc_lib.sh

report_init "NN: what this covers"
irc_setup
trap irc_teardown EXIT

irc_connect alice
irc_register alice alice
irc_send alice "JOIN #x"
expect_ok alice "JOIN" 2.0 "alice can join #x"

report_summary
```

Two rules the suite depends on:

- **Portable shell only.** No `${BASH_SOURCE[0]}`, no arrays, no `[[ ]]`,
  no `${var:i:1}`. `$0` and `dirname "$0"` work in every shell we target.
- **Namespace your variables inside library functions.** POSIX `sh` has no
  scoping and `local` is not portable, so everything in `irc_lib.sh` is
  prefixed `_irc_`. An unprefixed `i` inside `irc_expect()` silently destroys
  the caller's loop counter — which it used to do.

Nicks must be **9 characters or fewer**: the server advertises `NICKLEN=9` and
truncates anything longer, so a client registered as `probeclient` is actually
reachable only as `probeclie`. `irc_register` warns loudly about this now.

## Known shell differences

Under `bash` the suite is fully green. Under `hellish` 2.3.2 two scripts fail,
both from shell defects rather than server defects:

| Script | Cause | Reported |
|---|---|---|
| `00_shell_probe.sh` | Command substitution is brace-expanded and executed once per alternative, which destroys `awk 'BEGIN{ printf "%d", n }'`. | [hellish#11](https://github.com/Univers42/hellish/issues/11) |
| `04_disconnect.sh` | `$!` returns an intermediate wrapper pid, so `kill -9 "$!"` orphans the real `nc` and the client never disconnects. | [hellish#13](https://github.com/Univers42/hellish/issues/13) |

Also filed, and worked around in this suite:
[#12](https://github.com/Univers42/hellish/issues/12) `export A B C` corrupts
variables (hence one `export` per line in `config.sh`),
[#14](https://github.com/Univers42/hellish/issues/14) `$0` empty under `-c`,
[#15](https://github.com/Univers42/hellish/issues/15) `${var:?}` prints an
empty diagnostic.
