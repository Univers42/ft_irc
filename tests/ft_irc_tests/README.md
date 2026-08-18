# ft_irc test suite

A set of small, readable scripts that hammer on your `ircserv` and tell you
what broke. Nothing fancy — raw sockets, `nc`-style bash checks where that's
enough, a tiny Python helper where you need precise control over TCP framing.
Goal is repair, not ceremony: run it, read the `[FAIL]` lines, go fix that
one thing, run it again.

## Quick start

```bash
cd ft_irc_tests
$EDITOR config.sh          # set PROJECT_DIR to your ft_irc repo root
./run_all.sh                # builds, starts your server, runs everything
./run_all.sh --skip-build   # reuse the existing binary
./run_all.sh --with-valgrind  # also run the memory-check pass at the end
```

`config.sh` is the only file you should need to edit:

```bash
PROJECT_DIR   # folder containing your Makefile and src/
BIN           # path to the built binary (defaults to $PROJECT_DIR/ircserv)
IRC_HOST      # 127.0.0.1
IRC_PORT      # port the long-lived test server runs on
IRC_PASSWORD  # password the test server is started with
STARTUP_PORT  # separate port used only by the argv/startup tests
```

### Don't have a working server yet? Test the harness first

`tests/mock/mock_ircserv.py` is a throwaway, non-compliant, pure-Python stand-in
server (NOT a reference implementation — don't peek at it for project ideas,
it skips almost everything the subject requires). It exists so you can
confirm the test scripts themselves work before you point them at your real
binary and start debugging phantom failures that are actually typos in a
regex:

```bash
BIN=./tests/mock/mock_ircserv.py ./run_all.sh --skip-build
```

Every test in this suite currently passes cleanly against that mock, so if
your real `ircserv` fails one, the bug is almost certainly in your server.

## Layout

```
ft_irc_tests/
├── config.sh                      # edit this
├── run_all.sh                     # orchestrates everything, prints a summary
├── lib/irc_client.py              # tiny socket client + pass/fail reporter, shared by all .py tests
└── tests/
    ├── 00_build_norm.sh           # Makefile targets, warnings, C++98, forbidden fns, single poll()
    ├── 01_startup.sh              # argv validation, bind, port-in-use
    ├── 02_registration.py         # PASS/NICK/USER handshake + edge cases
    ├── 03_tcp_framing.py          # split reads, batched commands, bad line endings, garbage
    ├── 04_disconnect.py           # abrupt disconnects: nick freed, channel cleaned up
    ├── 05_privmsg.py              # user + channel messaging, error cases
    ├── 06_channel_join_part.py    # JOIN / PART semantics
    ├── 07_kick_invite_topic.py    # KICK, INVITE (+i), TOPIC (+t), operator gating
    ├── 08_modes.py                # +i +t +k +o +l individually, combined, malformed
    ├── 09_malformed_preauth.py    # commands before registration, unknown commands, missing params
    ├── 10_stress_multiclient.py   # many clients + the "one slow client can't freeze the rest" test
    ├── 11_memory_checks.sh        # optional: valgrind wrapper, parses the leak/error summary
    └── mock/mock_ircserv.py       # throwaway server for testing the tests
```

Each `.py` file can also be run on its own once a server is up:

```bash
IRC_PORT=6667 IRC_PASSWORD=pass python3 tests/08_modes.py
```

## What's actually being checked, by area

- **Build (`00`)** — `make`/`re`/`clean`/`fclean` all succeed, zero warnings,
  `-std=c++98` pinned, no `fork`/`exec*`/`pthread_create`, and **exactly one**
  `poll`/`select`/`epoll_wait`/`kevent` call anywhere in `src/` (this is your
  own grep check, cleaned up and turned into a proper pass/fail with a
  summary — kept because it's a genuinely good static gate).
- **Startup (`01`)** — bad invocations (no args, non-numeric port,
  out-of-range port, port already bound, too many args) exit cleanly and
  quickly instead of hanging or segfaulting; a good invocation actually binds
  and accepts a connection.
- **Registration (`02`)** — correct/incorrect password, missing `PASS`
  entirely, duplicate nicknames, invalid nicknames, a 500-character nickname
  (shouldn't crash anything), re-sending `PASS` after already registered,
  changing nick post-registration.
- **TCP framing (`03`)** — the big one. A command sent one byte at a time,
  several commands in a single packet, a command with the start of the next
  one glued on, `\n`-only line endings, blank lines between commands, a 20 KB
  single line, and raw garbage bytes before a valid command. If your parser
  assumes "one `recv()` = one command," this file will find it.
- **Disconnect cleanup (`04`)** — hard-closing a socket (no `QUIT`) frees the
  nickname, notifies remaining channel members, and a third client can
  immediately claim the freed nick — which only works if you actually tore
  down the old client's state and not just the fd.
- **PRIVMSG (`05`)** — user-to-user and user-to-channel delivery, unknown
  nick, missing target/text, messaging a channel you haven't joined.
- **JOIN/PART (`06`)** — creating vs. joining an existing channel,
  notifications to existing members, re-JOINing a channel you're already in,
  comma-separated multi-channel JOIN, PART cleanup and rejoin.
- **KICK/INVITE/TOPIC (`07`)** — non-operator attempts are refused; operator
  actions actually take effect (kicked user is *removed*, not just told
  about it); `+i` blocks JOIN until invited; `+t` gates who can set the
  topic; a newly-joining member is shown the current topic.
- **MODE (`08`)** — `+i -i +t -t +k -k +o -o +l -l` each tested for the
  permission check *and* the functional effect (does JOIN actually get
  blocked/allowed, is the password actually required, is the limit actually
  enforced), plus a combined `+ikl <key> <limit>` command and malformed MODE
  (missing arguments, unknown mode letters).
- **Malformed input / pre-auth (`09`)** — every privileged command sent
  before registration should bounce off `451`, not execute; unknown commands
  should get a clean error, not silence-then-crash; missing required params
  shouldn't take the server down; an empty line shouldn't either.
- **Stress (`10`)** — 15 clients registering and joining the same channel at
  once; a client that **never reads its socket** while another client floods
  the channel with ~240 KB of traffic, checked against an unrelated client
  that must keep getting its own messages promptly (this is the test that
  catches a blocking `send()` hiding somewhere in your write path); clients
  disconnecting abruptly mid-session while others keep talking.
- **Memory (`11`, opt-in)** — runs the functional suite with the server under
  `valgrind --leak-check=full`, then greps the summary for lost bytes and
  error counts.

## Assumptions baked into the scripts

- **First person to `JOIN` a brand-new channel becomes its operator.** This
  is the common convention and is what `07`/`08` assume when they check
  "operator-only" behavior. If your implementation does channel-operator
  assignment differently, adjust those two files or grant op manually first.
- **Numeric replies** are checked loosely (e.g. `r"401|No such nick"`) rather
  than pinned to one exact string, since exact wording varies. The numerics
  used as references throughout (see table below) are the standard RFC 2812
  ones; if your server uses different codes on purpose, that's a
  spec-compliance question worth settling separately from "does this crash."
- Tests assume a **persistent server the whole file can talk to** — `run_all.sh`
  starts one instance and reuses it across `02`–`10`. If a test crashes your
  server, every test after it in the run will report failures too (that's
  useful signal, not a bug in the harness — check the *first* failure, not
  the last).

## IRC numerics quick reference

| Code | Meaning                          | Used by       |
|------|-----------------------------------|---------------|
| 001  | Welcome (registration complete)   | 02, 03        |
| 401  | No such nick/channel              | 05            |
| 403  | No such channel                   | 08            |
| 404  | Cannot send to channel             | 05, 07        |
| 411  | No recipient given                 | 05            |
| 412  | No text to send                    | 05            |
| 421  | Unknown command                    | 09            |
| 431  | No nickname given                  | 02            |
| 432  | Erroneous nickname                 | (not enforced by these scripts — add if your server checks this) |
| 433  | Nickname already in use            | 02            |
| 441  | User not in channel                | 07 (mock only)|
| 442  | Not on that channel                | 05, 06, 07    |
| 451  | Not registered                     | 09            |
| 461  | Not enough parameters              | 02, 05, 06, 08, 09 |
| 462  | Already registered                 | 02            |
| 471  | Channel is full (+l)               | 08            |
| 472  | Unknown mode char                  | 08            |
| 473  | Invite-only channel (+i)           | 07, 08        |
| 475  | Bad channel key (+k)               | 08            |
| 482  | Not channel operator                | 07, 08        |

## Manual pre-submission pass

Scripted tests catch regressions; they don't replace ten minutes of you
actually watching five terminals talk to each other. Before submitting, run
this by hand once with `nc` or your favorite IRC-ish client:

```
Alice, Bob, Charlie, Dave, Eve all register.

Alice   JOIN #42
Bob     JOIN #42
Charlie JOIN #42
Dave    JOIN #42
Eve     JOIN #other

Alice   MODE #42 +it
Alice   TOPIC #42 :Test channel
Alice   KICK #42 Bob
Alice   INVITE Bob #42
Bob     JOIN #42
Alice   MODE #42 +k secret
Charlie JOIN #42            (should fail, no key)
Charlie JOIN #42 secret     (should succeed)
Alice   MODE #42 +l 2

Then simultaneously:
  - Alice and Bob send messages
  - Charlie disconnects mid-session
  - Dave joins/leaves repeatedly
  - Eve sends private messages from #other
  - Bob changes nickname
  - Alice flips modes
  - one client sends a deliberately malformed command
  - one client sends a command split across two writes
  - one client stops reading entirely
```

If the server is still answering everyone correctly at the end of that, and
`valgrind` comes back clean, you're in good shape.

## Memory / sanitizers

```bash
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
    ./ircserv 6667 password
```

then drive it with the harness (`./run_all.sh --with-valgrind` does this for
you). Separately, consider adding a debug Makefile target compiled with
`-fsanitize=address,undefined` — ASan/UBSan catch a different class of bugs
(use-after-free, invalid reads, signed overflow) than valgrind's leak
detector and run fast enough to leave on during normal development.

## The three areas most likely to bite you

Based on how these things usually go wrong in practice: **partial TCP
reads** (`03`), **MODE argument parsing and the state it touches** (`08`),
and **cleanup on disconnect** (`04`, `10`). Implementations that look
perfect in a live demo tend to have exactly one of these three quietly
broken. Start there if you're short on time.
