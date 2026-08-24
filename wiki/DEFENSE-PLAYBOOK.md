# ft_irc — Defense Playbook

*Every line of the 42 evaluation sheet, turned into commands you can run, with
the output to expect. Nothing from the sheet is omitted.*

Work through it in order. Sections **1** and **2** are the instant-zero gates —
if anything there fails, the defense is over and nothing else matters.

| § | Sheet section | Zero risk |
| --- | --- | --- |
| [0](#0-setup) | Tooling & terminal layout | — |
| [1](#1-pre-flight) | Guidelines: repo, clone, aliases | **-42 for cheating** |
| [2](#2-basic-checks) | Basic checks | **instant 0** |
| [3](#3-networking) | Networking | grade |
| [4](#4-networking-specials) | Networking specials | **0 on crash/leak** |
| [5](#5-registration-matrices) | Client commands basic | grade |
| [6](#6-privmsg-matrix) | PRIVMSG | grade |
| [7](#7-channel-operator-commands) | Channel operator | −1 per broken feature |
| [8](#8-the-mode-matrix) | MODE, all 32 combinations | grade |
| [9](#9-grammar-torture) | Malformed input | **0 on crash** |
| [10](#10-stress--memory) | Leaks & stress | **0 on leak** |
| [11](#11-bonus--file-transfer) | Bonus | only if mandatory is perfect |

---

## 0. Setup

### Terminal layout

Use four terminals. Do not improvise this during the defense.

| # | Role |
| --- | --- |
| **T1** | the server, running in the foreground so a crash is visible |
| **T2** | `nc` client A |
| **T3** | `nc` client B |
| **T4** | inspection — `ss`, `ps`, `grep`, loops |

### Build and launch

```bash
make re                       # must be warning-free
./build/bin/ircserv 6667 pass
```

For the parts that need to *see* the protocol:

```bash
FT_IRC_LOG=trace ./build/bin/ircserv 6667 pass


#or
FT_IRC_LOG=trace strace -f -tt -T -s 256 -yy -o strace.log ./build/bin/ircserv 6667 pass


#or
FT_IRC_LOG=trace strace -f -tt -T -s 1024 -yy -v -o strace.log ./build/bin/ircserv 6667 pass


#or

FT_IRC_LOG=trace strace -f -tt -T -s 1024 -yy \
  -e trace=network,read,write,close \
  ./build/bin/ircserv 6667 pass

```

### The one flag that matters for nc

```bash
nc -C 127.0.0.1 6667
```

`-C` sends **CRLF** line endings. Without it `nc` sends bare `LF`, and while
this server tolerates that, the RFC does not — use `-C` so you are testing the
real protocol. If your `nc` lacks `-C`:

```bash
printf 'PASS pass\r\nNICK alice\r\nUSER a 0 * :A\r\n' | nc 127.0.0.1 6667
```

### Run this in bash

Every loop in this document relies on POSIX **word splitting** (`./build/bin/ircserv $a`
with `a="abc pass"` must become two arguments). `hellish` — this project's own
shell, and possibly your login shell — does not split unquoted expansions, so
those loops silently degrade into a single-argument call and every row reports
the same wrong answer. Start the session with:

```bash
bash          # not hellish, not dash
echo $BASH_VERSION
```

### Pick a port nothing else is using

`tests/run_all.sh` binds **6667** and its cleanup runs
`pgrep -x ircserv | xargs -r kill`, which kills *every* process named `ircserv`
regardless of port. If a test suite, a CI job or a second terminal may be
running, take a private port (`6767` below) so nothing pulls the server out from
under you mid-check.

### A reusable one-shot helper

Paste this into T4 once. Every "expected output" below assumes it.

```bash
irc() {  # irc <port> <line>...   — registers nothing, sends exactly what you give
  local port=$1; shift
  { for l in "$@"; do printf '%s\r\n' "$l"; sleep 0.15; done; sleep 0.5; } \
    | nc -q1 127.0.0.1 "$port"
}
reg() {  # reg <port> <nick>  — a fully registered session, then extra lines
  local port=$1 nick=$2; shift 2
  irc "$port" "PASS pass" "NICK $nick" "USER $nick 0 * :$nick" "$@"
}
```

---

## 1. Pre-flight

> *"Double-check that the Git repository belongs to the student(s)… check that
> `git clone` is used in an empty folder… check carefully that no malicious
> aliases was used."*

Run these **before** touching the code. This is the grader's job; make it easy.

```bash
mkdir -p /tmp/eval && cd /tmp/eval && ls -A          # MUST be empty
git clone <repo-url> . && git log --oneline -5
git remote -v                                        # points at the student's repo

# aliases and shell functions that could fake a build or a test
alias | grep -iE 'make|ircserv|valgrind|nc|git'      # expect: no output
type -a make ircserv nc valgrind 2>/dev/null | grep -v '^make is /usr'
declare -f | head                                    # expect: no output

# submodules — this project vendors libcpp
#   NOT --recursive: libcpp's own nested vendor/scripts is pinned at a commit
#   that was never pushed, so --recursive aborts and leaves vendor/libcpp
#   unchecked-out. CI initialises exactly these two, for exactly this reason.
git submodule update --init vendor/libcpp vendor/googletest
```

> **Check this before defense day.** At the time of writing, a clone made this
> way still does not compile: six files the build needs live only in the
> developer's local `vendor/libcpp` working tree and are not in the pinned
> commit. See [`../err_log.md`](../err_log.md) §1 — it is the first thing to
> fix, because §2.1 below is an instant-zero gate.

| Check | Pass | Fail |
| --- | --- | --- |
| Folder empty before clone | `ls -A` prints nothing | anything present → reclone elsewhere |
| Remote is the student's | `git remote -v` matches | mismatch → **flag cheating** |
| No aliases | `alias \| grep` empty | an alias for `make` → **flag cheating** |
| Not empty repo | files present | empty → **flag empty repo, grade 0** |

---

## 2. Basic checks

> *"If any of these points is wrong, the evaluation ends now and the final mark
> is 0."*

### 2.1 Makefile, flags, C++98, executable name

```bash
grep -E '^(NAME|CXX|CXXFLAGS)' Makefile
make re 2>&1 | tee /tmp/build.log
grep -icE 'warning:|error:' /tmp/build.log   # expect 0
ls -l ircserv                                 # the executable must be named exactly this
file ircserv
```

Match `warning:` / `error:` **with the colon** — that is the shape of a compiler
diagnostic. A bare `grep -icE 'warning|error'` can never return 0 here, because
the build banner itself prints the flag `-Werror`:

```
-- ft_irc - full tier - c++ -Wall -Wextra -Werror -std=c++98
```

**Expected:** `-Wall -Wextra -Werror -std=c++98` present, zero warnings, and an
executable called `ircserv`.

The binary is at `build/bin/ircserv`, and that is the only copy — every
generated file lives under `build/` and the repo root stays source-only. Run it
as `./build/bin/ircserv <port> <password>`. Say so before the grader asks;
`ls -l build/bin/ircserv` shows the ELF binary, and `ls ircserv` finds nothing.

Required rules — all five must exist:

```bash
for t in all clean fclean re bonus; do
  grep -qE "^$t:" Makefile && echo "  ok   $t" || echo "  MISSING $t"
done
make re >/dev/null && make | grep -qi 'up to date\|nothing to be done' \
  && echo "  ok   does not relink" || echo "  RELINKS"
```

C++98 conformance, proven rather than claimed:

```bash
make fclean && make CXXFLAGS="-Wall -Wextra -Werror -std=c++98" 2>&1 | tail -5
grep -rnE '\bnullptr\b|\bauto \b|unique_ptr|\[\[|= *default;|= *delete;' src include | head
# expect: no hits (or only inside comments)
```

### 2.2 Exactly one poll/epoll — the classic instant zero

```bash
grep -rnE '\b(poll|epoll_wait|select|kqueue)\s*\(' src/ include/ | grep -v '"'
```

**Expected: exactly one line.**

```
src/Server.cpp:273:    int nfds = epoll_wait(_reactor.fd(), events, MAX_EVENTS, 1000);
```

The `| grep -v '"'` is not cheating and you should explain it rather than hide
it: the very next line is the error path,

```cpp
throw std::runtime_error("epoll_wait() failed: " + std::string(strerror(errno)));
```

and the words `epoll_wait(` appear inside that **string literal**, so a raw grep
reports two hits for one call site. Dropping lines containing a quote leaves the
call itself. `scripts/audit.sh` filters the same way and prints
`✓ exactly 1 event-wait call site`.

Say this out loud to the grader: *"one `epoll_wait`, in `Server::run`.
`epoll_ctl` appears more than once, but that is registration, not waiting."*

Line numbers move; let grep print them rather than trusting the ones written
here.

```bash
grep -rn 'epoll_ctl' src/ vendor/libcpp/c98/src/reactor.cpp   # registration only
bash scripts/audit.sh | grep -i 'event-wait'
# ✓ exactly 1 event-wait call site
```

#### What `bircd` is for, and how to answer if asked

The school hands out `bircd.tar.gz` with this subject. It is **not** a tester
and the subject text never mentions it — 285 lines of C whose `client_read.c`
does `recv()` then `send()`s the raw bytes to every other client. Feed it a
registration burst and the sender gets nothing back while everyone else gets
`PASS pw\r\nNICK alice\r\n…` verbatim. It has no parser, no numerics, no
assertions. (It also will not start on a modern box: `init_env.c` allocates
`sizeof(t_fd) * RLIMIT_NOFILE`, about 8 GB at a soft limit of 1048576. Run it
under `ulimit -n 1024`. To unpack it: it is **double**-gzipped, so
`gunzip -c bircd.tar.gz | gunzip -c | tar -x` — plain `tar -xzf` fails.)

What it is, is the worked example of the one rule the subject attaches a zero
to: *"if you attempt to read/recv or write/send in any file descriptor without
using poll() (or equivalent), your grade will be 0."* Its three phases are the
shape that obeys it, and ours maps onto them one for one:

| bircd | ft_irc | what it does |
| --- | --- | --- |
| `init_fd()` | `updateEpollInterest()` | declare interest: read always, write **only** when output is buffered — `strlen(buf_write) > 0` there, `hasPendingData()` here |
| `do_select()` | `epoll_wait()` in `Server::run` | the single event wait |
| `check_fd()` | the `for (nfds)` dispatch | act **only** on readiness — `FD_ISSET` there, `ev & EPOLLIN/EPOLLOUT` here |

`scripts/check_event_loop.py` asserts exactly that, and runs in the audit:

```bash
python3 scripts/check_event_loop.py            # static: one wait, all I/O behind it
python3 scripts/check_event_loop.py --runtime  # strace the live process
```

The runtime mode is the strong one — it records the syscall order from the
running server and fails if any `recv`/`send`/`accept` happens before an
`epoll_wait` has returned. Both modes were negative-tested against a
deliberately blind server that `recv()`s with no event wait at all, which is
precisely the graded-zero shape; it reports seven violations.

### 2.3 poll before every accept / recv / send, and no errno-driven retry

> *"After these calls, errno should not be used to trigger specific action
> (e.g. like reading again after `errno == EAGAIN`)."*

Show the grader the call graph — every I/O call is reached **only** from the
event dispatch:

```bash
grep -nE '\b(accept|recv|send)\s*\(' src/*.cpp src/**/*.cpp
```

| Call | Line (at the time of writing) | Reached only from |
| --- | --- | --- |
| `accept` | `Server.cpp:309` | `acceptClient()` ← `EPOLLIN` on `_listenFd` |
| `recv` | `Server.cpp:348` | `handleClientInput()` ← `EPOLLIN` on a client |
| `send` | `Server.cpp:378` | `handleClientOutput()` ← `EPOLLOUT` on a client |

Then prove no `errno` steering:

```bash
grep -n 'EAGAIN\|EWOULDBLOCK\|EINTR' src/*.cpp src/**/*.cpp
```

**Expected: exactly one hit**, and it is *not* on an I/O path:

```
src/Server.cpp:275:      if (errno == EINTR) continue;   // epoll_wait interrupted by a signal
```

Say: *"`EINTR` on `epoll_wait` is not I/O retry — it is the required way to
resume a syscall a signal interrupted. No `recv`/`send` ever inspects `errno`;
they return and wait for the next event."*

Check the read path returns instead of looping (grep, not a fixed line range —
the numbers drift):

```bash
grep -n -A4 'ssize_t bytesRead = recv' src/Server.cpp
```

```c
ssize_t bytesRead = recv(fd, buf, Limits::kMsgLen, 0);
if (bytesRead <= 0) {
    if (bytesRead == 0) disconnectClient(fd, "Connection closed");
    return;                      // <-- no retry, no errno branch
}
```

### 2.4 fcntl used only as `F_SETFL, O_NONBLOCK`

```bash
grep -rn 'fcntl' src/ include/
```

**Expected: five hits — two calls, two error strings, one `#include`.** Only the
two calls matter, and both carry `F_SETFL, O_NONBLOCK`:

```
src/Server.cpp:4:#include <fcntl.h>
src/Server.cpp:233:  if (fcntl(_listenFd, F_SETFL, O_NONBLOCK) < 0)
src/Server.cpp:234:    throw std::runtime_error("fcntl() failed: " + std::string(strerror(errno)));
src/Server.cpp:318:  if (fcntl(clientFd, F_SETFL, O_NONBLOCK) < 0) {
src/Server.cpp:319:    Log::error() << "fcntl() failed on client fd: " << strerror(errno);
```

To see just the calls: `grep -rn 'fcntl *(' src/ | grep -v '"'`.

Any `F_GETFL`, `F_SETFD`, `FD_CLOEXEC` or `O_APPEND` → **grade 0**.

```bash
grep -rnE 'F_GETFL|F_SETFD|F_GETFD|FD_CLOEXEC|O_APPEND' src/ include/ \
  && echo "VIOLATION" || echo "  ok   only F_SETFL/O_NONBLOCK"
bash scripts/audit.sh | grep -i fcntl
```

### 2.5 Basic-checks control matrix

| # | Claim | Command | Expected | Zero if |
| --- | --- | --- | --- | --- |
| 2.1 | builds clean | `make re` | no warnings | warnings with `-Werror` |
| 2.1 | binary name | `ls ircserv` | exists | different name |
| 2.1 | C++98 | `make CXXFLAGS=…-std=c++98` | builds | fails |
| 2.2 | one event wait | `grep -rE 'poll\|epoll_wait\|select'` | 1 line | 2+ lines |
| 2.3 | no EAGAIN retry | `grep EAGAIN` | 0 hits | any hit on an I/O path |
| 2.4 | fcntl form | `grep fcntl` | 2 hits, both `F_SETFL, O_NONBLOCK` | any other flag |

---

## 3. Networking

### 3.1 Listens on all interfaces, on the port from argv

```bash
./build/bin/ircserv 6667 pass &
ss -ltnp | grep 6667
```

**Expected** — `0.0.0.0:*` means *all* interfaces, which is what the sheet asks:

```
LISTEN 0  4096  0.0.0.0:6667  0.0.0.0:*  users:(("ircserv",pid=…,fd=3))
```

Prove the port really comes from the command line, and that bad input is
rejected rather than defaulted:

```bash
./build/bin/ircserv 7777 pass & sleep 0.3; ss -ltn | grep -c 7777; kill %1
```

| # | Invocation | Expected | Why |
| --- | --- | --- | --- |
| 1 | `./build/bin/ircserv 6667 pass` | listens | nominal |
| 2 | `./build/bin/ircserv 7777 pass` | listens on 7777 | port really from argv |
| 3 | `./build/bin/ircserv` | usage error, exit ≠ 0 | too few args |
| 4 | `./build/bin/ircserv 6667` | usage error | missing password |
| 5 | `./build/bin/ircserv 6667 pass extra` | usage error | too many args |
| 6 | `./build/bin/ircserv abc pass` | error, no crash | non-numeric port |
| 7 | `./build/bin/ircserv 0 pass` | error or ephemeral | port 0 |
| 8 | `./build/bin/ircserv 65536 pass` | error | out of range |
| 9 | `./build/bin/ircserv -1 pass` | error | negative |
| 10 | `./build/bin/ircserv 80 pass` (non-root) | clean "bind failed", no crash | privileged port |

```bash
# bash only — `./build/bin/ircserv $a` depends on word splitting (see §0)
for a in "" "6667" "6667 pass extra" "abc pass" "0 pass" "65536 pass" "-1 pass" "80 pass"; do
  printf '%-22s -> ' "[$a]"
  out=$(./build/bin/ircserv $a 2>&1 | head -1); ./build/bin/ircserv $a >/dev/null 2>&1
  printf '%-58s exit=%s\n' "$out" "$?"
done
```

Verified output — note that rows 4–9 must say **port**, not **usage**; if every
row says `usage:` you are not in bash and the arguments never got split:

```
[]                     -> [ircserv] error: usage: ./build/bin/ircserv <port> <password>        exit=1
[6667]                 -> [ircserv] error: usage: ./build/bin/ircserv <port> <password>        exit=1
[6667 pass extra]      -> [ircserv] error: usage: ./build/bin/ircserv <port> <password>        exit=1
[abc pass]             -> [ircserv] error: port must be a number between 1 and 65535 exit=1
[0 pass]               -> [ircserv] error: port must be a number between 1 and 65535 exit=1
[65536 pass]           -> [ircserv] error: port must be a number between 1 and 65535 exit=1
[-1 pass]              -> [ircserv] error: port must be a number between 1 and 65535 exit=1
[80 pass]              -> [ircserv] error: fatal: bind() failed: Permission denied   exit=1
```

Row 7 (`0 pass`) is rejected outright rather than being bound as an ephemeral
port — `parse_long(portStr, 1, 65535, port)` in `main.cpp` sets the floor at 1.
The empty password is caught too, but you must quote it or the shell eats it:

```bash
./build/bin/ircserv 6667 ""      # [ircserv] error: password cannot be empty   exit=1
```

**Every one must exit cleanly. A segfault here ends the defense.**

### 3.2 Connect with nc and get an answer

T2:

```bash
nc -C 127.0.0.1 6667
PASS pass
NICK alice
USER alice 0 * :Alice Liddell
```

**Expected** — the full welcome burst:

```
:ft_irc 001 alice :Welcome to the ft_irc Network alice!alice@127.0.0.1
:ft_irc 002 alice :Your host is ft_irc, running version 1.0
:ft_irc 003 alice :This server was created 2025-01-01
:ft_irc 004 alice ft_irc 1.0 o itkol
:ft_irc 005 alice CHANTYPES=# PREFIX=(o)@ CHANMODES=,,kl,it NICKLEN=9 … :are supported
:ft_irc 422 alice :MOTD File is missing
```

The trailing **422** is expected and correct: there is no MOTD file, and the RFC
requires the server to say so rather than stay silent. Do not let a grader read
it as an error.

`004`'s last token `itkol` is the supported channel-mode set — point at it when
you reach §8.

### 3.3 Reference IRC client

> *"Ask the team what is their reference IRC client."*

**Answer: HexChat.** Have it pre-configured — do not fumble this live.

**Network List → Add** → name `ft_irc` → **Edit**:

| Field | Value |
| --- | --- |
| Server | `127.0.0.1/6667` |
| Password | `pass` |
| Nickname | ≤ 9 characters |
| Uncheck | *Use SSL*, *Connect to selected server only* |

### 3.4 Multiple simultaneous connections, nc and HexChat together

> *"The server should not block. It should be able to answer all demands."*

```bash
# T4 — ten simultaneous registered clients, while HexChat is also connected
for i in $(seq 1 10); do
  ( reg 6667 "bot$i" "JOIN #eval" "PRIVMSG #eval :hello from bot$i" ) &
done; wait
ss -tn | grep -c 6667      # connection count while they run
```

Then, still in HexChat, type in `#eval`. **Expected:** HexChat sees all ten
`hello from botN` lines and its own messages reach every `nc`.

### 3.5 Channel broadcast

T2 (alice) and T3 (bob), both registered:

```
JOIN #eval
```

**alice sees, on bob's join:**

```
:bob!bob@127.0.0.1 JOIN #eval
```

**alice sends** `PRIVMSG #eval :hello` → **bob sees**:

```
:alice!alice@127.0.0.1 PRIVMSG #eval :hello
```

**alice does *not* see her own PRIVMSG echoed** — that is correct IRC behaviour,
and graders often query it. The sender's client displays its own message
locally; the server must not echo it back.

| # | Action | alice sees | bob sees |
| --- | --- | --- | --- |
| 1 | bob JOINs | `:bob… JOIN #eval` | JOIN + 353 + 366 |
| 2 | alice PRIVMSG | *nothing* | the message |
| 3 | bob PRIVMSG | the message | *nothing* |
| 4 | bob PARTs | `:bob… PART #eval` | `:bob… PART #eval` |
| 5 | bob QUITs | `:bob… QUIT :…` | connection closed |

---

## 4. Networking specials

### 4.1 Partial commands

> *"Using nc, try to send partial commands. Check that the server answers
> correctly. With a partial command sent, ensure that other connections still
> run fine."*

The point is that TCP is a **byte stream** — a command can arrive in any number
of pieces. Demonstrate deliberately:

```bash
# T2 — one command dribbled out one fragment at a time, 2s apart
{ printf 'PASS pass\r\nNICK ali'; sleep 2;
  printf 'ce\r\nUSER a 0 '; sleep 2;
  printf '* :A\r\n'; sleep 2; } | nc -C 127.0.0.1 6667
```

**Expected:** nothing happens until each `\r\n` lands, then the welcome burst
appears. **While those 6 seconds elapse, T3 must stay fully responsive** — this
is the actual thing being graded.

```bash
# T3, run DURING the sleeps above
reg 6667 carol "JOIN #eval" "PRIVMSG #eval :still alive"
```

| # | Fragmentation | Expected |
| --- | --- | --- |
| 1 | `NICK ali` + `ce\r\n` | one `NICK alice` |
| 2 | one byte at a time | works, just slow |
| 3 | `\r` and `\n` split across sends | one command |
| 4 | 3 commands in one write | all 3 executed, in order |
| 5 | `PRIVMSG #c :hel` then `lo\r\n` | one message `hello` |
| 6 | 600 bytes with no `\r\n` | truncated at 512, no crash |
| 7 | only `\r\n` | ignored, no error |
| 8 | `\n` alone (no `\r`) | accepted (lenient) |
| 9 | NUL byte mid-line | stripped or rejected, no crash |
| 10 | never terminated, then close | clean disconnect, no leak |

```bash
# 4 — three commands in a single write
printf 'PASS pass\r\nNICK m\r\nUSER m 0 * :M\r\n' | nc -q1 127.0.0.1 6667

# 6 — 600 bytes, no terminator
{ printf 'PASS pass\r\nNICK big\r\nUSER b 0 * :B\r\n';
  python3 -c "print('PRIVMSG #x :' + 'A'*600, end='')"; sleep 1; } | nc -q1 127.0.0.1 6667

# 10 — half a command, then hard close
{ printf 'PASS pass\r\nNICK half\r\nUSER h 0 * :H\r\nPRIV'; sleep 0.5; } | nc -q1 127.0.0.1 6667
```

### 4.2 Kill a client unexpectedly

```bash
nc -C 127.0.0.1 6667 &   NCPID=$!
# register it, join #eval, then:
kill -9 $NCPID
```

**Expected in the server log:** a clean disconnect, no crash.

```
client disconnected: dead!dead@127.0.0.1 fd=6 registered tearing-down (Connection error)
```

Others in `#eval` receive `:dead!dead@127.0.0.1 QUIT :Connection error`, and a
**new** client can still connect afterwards — test that explicitly:

```bash
reg 6667 afterkill "JOIN #eval" "PRIVMSG #eval :server survived"
```

### 4.3 Kill nc with half a command sent

```bash
{ printf 'PASS pass\r\nNICK zz\r\nUSER z 0 * :Z\r\nPRIVMSG #eval :incomp'; sleep 5; } \
  | nc -C 127.0.0.1 6667 &
sleep 1; kill -9 %1
reg 6667 probe "JOIN #eval" "PRIVMSG #eval :not blocked"
```

**Expected:** the partial line is discarded with the client. No hang, no
half-message delivered, `probe` works normally.

### 4.4 Stop a client with ^Z, flood it, resume

> *"The server should not hang. When the client is live again, all stored
> commands should be processed normally. Also, check for memory leaks."*

This is the **backpressure** test and the one most servers fail.

```bash
# T2 — a client that joins and is then frozen
nc -C 127.0.0.1 6667
PASS pass
NICK slow
USER slow 0 * :Slow
JOIN #flood
^Z                      # <-- SIGTSTP: it stops reading, kernel buffer fills

# T3 — flood the channel
reg 6667 loud "JOIN #flood"
for i in $(seq 1 5000); do printf 'PRIVMSG #flood :flood %d\r\n' $i; done \
  | nc -q2 -C 127.0.0.1 6667
```

**While frozen, verify in T4 that the server is still healthy:**

```bash
ss -tn | grep 6667                       # Send-Q on slow's socket grows
top -b -n1 -p $(pgrep ircserv) | tail -2 # CPU must NOT be pegged at 100%
reg 6667 witness "JOIN #flood" "PRIVMSG #flood :server responsive"
```

Then resume: `fg` in T2 → the buffered messages flush.

**The CPU check is the real test.** A server that permanently subscribes to
`EPOLLOUT` spins at 100% here. This one only requests `EPOLLOUT` while data is
queued (`Server.cpp:415`), so CPU stays near zero.

If the queue passes SendQ (64 KiB, `Limits.hpp`), the client is dropped with
`Max SendQ exceeded` — that is correct, not a bug. Explain it before the grader
asks.

**Leak check across the whole operation** — see [§10](#10-stress--memory).

---

## 5. Registration matrices

The registration handshake is `PASS` → `NICK` → `USER`. Nothing else works
until it completes — every other command returns **451 ERR_NOTREGISTERED**.

```bash
irc 6667 "JOIN #early"
# :ft_irc 451 * :You have not registered
```

### 5.1 PASS

| # | Command | Expected | Why |
| --- | --- | --- | --- |
| 1 | `PASS pass` | silence, then NICK/USER work | ✅ correct password |
| 2 | `PASS :pass` | same | ✅ grammar allows a leading `:` |
| 3 | `PASS pass` ×2 before register | **462** ALREADYREGISTRED | ✅ second PASS refused |
| 4 | `PASS wrong` then NICK+USER | **464** PASSWDMISMATCH, closed | ❌ wrong password |
| 5 | `PASS` (no param) | **461** NEEDMOREPARAMS | ❌ missing argument |
| 6 | NICK+USER with **no** PASS | **464**, connection closed | ❌ password is mandatory |
| 7 | `PASS pass extra` | **461** or ignored tail | ❌ arity |
| 8 | `PASS ""` (empty) | **461** | ❌ empty is not a password |
| 9 | `PASS PASS` | treated as the literal word | ✅ not a keyword |
| 10 | `PASS pass` *after* registering | **462** | ❌ too late |

```bash
irc 6667 "PASS wrong" "NICK w" "USER w 0 * :W"       # 464
irc 6667 "NICK np" "USER np 0 * :NP"                 # 464 — no PASS at all
irc 6667 "PASS"                                      # 461
reg 6667 late "PASS pass"                            # 462
```

### 5.2 NICK

Grammar (`EmbeddedGrammarSource.cpp:17`), which is where the rules come from:

```abnf
nickname = ( letter / special ) *8( letter / digit / special / "-" )
special  = %x5B-60 / %x7B-7D          ; [ \ ] ^ _ `  and  { | }
```

Read it carefully: **max 9 characters**, the first may **not** be a digit or `-`.

| # | NICK | Expected | Why |
| --- | --- | --- | --- |
| 1 | `NICK alice` | accepted | ✅ plain |
| 2 | `NICK a` | accepted | ✅ 1 char is legal |
| 3 | `NICK abcdefghi` | accepted | ✅ exactly 9 |
| 4 | `NICK [alice]` | accepted | ✅ `[` `]` are `special` |
| 5 | `NICK a-b_c\|d` | accepted | ✅ `-` `_` `\|` all legal after the first char |
| 6 | `NICK abcdefghij` | **432** ERRONEUSNICKNAME | ❌ 10 chars — **rejected, not truncated** |
| 7 | `NICK 1alice` | **432** | ❌ may not start with a digit |
| 8 | `NICK -alice` | **432** | ❌ may not start with `-` |
| 9 | `NICK ali.ce` | **432** | ❌ `.` is not in the alphabet |
| 10 | `NICK` (no param) | **431** NONICKNAMEGIVEN | ❌ missing |
| 11 | `NICK alice` when taken | **433** NICKNAMEINUSE | ❌ collision |
| 12 | `NICK ALICE` when `alice` exists | **433** | ❌ casemapping is ASCII-insensitive |

```bash
for n in alice a abcdefghi '[alice]' 'a-b_c|d' abcdefghij 1alice -alice ali.ce; do
  printf '%-12s -> ' "$n"
  irc 6667 "PASS pass" "NICK $n" "USER u 0 * :U" 2>/dev/null \
    | grep -oE ' (001|431|432|433) ' | head -1
done
```

**Point 12 is worth showing off.** IRC casemapping is ASCII-insensitive, so
`ALICE` collides with `alice`:

```bash
reg 6667 alice "JOIN #x" &      # hold it open
sleep 0.5; irc 6667 "PASS pass" "NICK ALICE" "USER a 0 * :A"   # 433
```

### 5.3 USER — `USER <username> <mode> <unused> <realname>`

Grammar (`EmbeddedGrammarSource.cpp:53`):

```abnf
user-cmd = "USER" SPACE $username SPACE $usermode SPACE $unused SPACE ":" $realname *SPACE
```

Three things a grader will probe, and the honest answer for each:

1. **The `:` before realname is mandatory** in this grammar. `USER a 0 * A`
   is a syntax error — the realname is a trailing parameter and may contain
   spaces, so it must be introduced by `:`.
2. **`<mode>` and `<unused>` are parsed but never read.** The grammar requires
   them to be *present and well-formed*; the handler ignores their values. That
   is RFC 2812 conformant — see below.
3. **`<username>` is truncated to 10 octets** (`Limits::kUserLen`) and then
   validated, so an over-long username is accepted-and-shortened, unlike a
   nickname, which is rejected.

#### 5.3.0 Arity and form — four parameters, the fourth trailing

Exactly four parameters, and the fourth **must** be the trailing one. Anything
else is **461**. Three malformed shapes fall out of the same check, and they
look different but fail for the same reason.

| # | Command | Params seen | Expected | Why |
| --- | --- | --- | --- | --- |
| 1 | `USER a 0 * :Real` | 4, 4th trailing | ✅ **001** | nominal |
| 2 | `USER a 0 * :Real Name Jr.` | 4 | ✅ 001 | trailing absorbs spaces |
| 3 | `USER a 0 * :` | 4, empty trailing | ✅ 001 | empty realname is legal |
| 4 | `USER a 0 * :a:b:c` | 4 | ✅ 001 | only the first `:` is syntax |
| 5 | `USER a 0 * :!@#$%^&*()` | 4 | ✅ 001 | punctuation is data |
| 6 | `USER a 0 * Real` | 4, **no trailing** | ❌ **461** | realname would silently become just `Real` |
| 7 | `USER a 0 * x :y` | **5** | ❌ 461 | the trailing is no longer the realname slot |
| 8 | `USER u 0 :R :R2` | **3** | ❌ 461 | `<unused>` cannot begin with `:` — the colon opens the trailing |
| 9 | `USER a 0 :R` | 3 | ❌ 461 | same |
| 10 | `USER a 0` / `USER a` / `USER` | 2 / 1 / 0 | ❌ 461 | arity |

**Row 6 is the subtle one.** It *has* four space-separated tokens, so a naive
`split(' ')` implementation accepts it and stores the realname as `Real`,
silently discarding everything after the first space. The grammar catches it
because `realname` is declared as `trailing`, which is introduced by `:`.

**Row 8 is subtler still.** `USER u 0 :R :R2` looks like four tokens, but `:`
opens the trailing parameter — so the parser sees `u`, `0`, and the trailing
`R :R2`. Three parameters, not four.

```bash
for c in "USER a 0 * :Real" "USER a 0 * Real" "USER a 0 * x :y" "USER u 0 :R :R2" "USER a 0 :R"; do
  printf '  %-22s -> ' "$c"
  irc 6667 "PASS pass" "NICK n$RANDOM" "$c" | grep -oE ' (001|461) ' | head -1
done
```

#### 5.3.1 `<username>` matrix

The RFC 2812 `user` production, implemented verbatim in `IrcName.hpp:38`:

```abnf
user = 1*( %x01-09 / %x0B-0C / %x0E-1F / %x21-3F / %x41-FF )
```

Everything **except** NUL, LF, CR, SPACE and `@`. The first four cannot reach
the handler anyway — framing strips CR/NUL, the tokenizer splits on SPACE — so
**`@` is the exclusion doing the real work**: every relayed line is stamped
`nick!user@host`, and a username carrying `@` makes the prefix ambiguous about
where the host begins.

Note what this *permits*, and expect to be asked: `:` (0x3A) and `!` (0x21) are
both inside `%x21-3F` and are therefore **legal usernames**. Truncation to
`kUserLen` happens *before* validation, so an `@` past the cut is simply gone.

| # | USER command | Expected | Why |
| --- | --- | --- | --- |
| 1 | `USER alice 0 * :A` | prefix `alice!alice@127.0.0.1` | ✅ plain |
| 2 | `USER a 0 * :A` | accepted | ✅ 1 char |
| 3 | `USER abcdefghij 0 * :A` | accepted, exactly 10 | ✅ at the cap |
| 4 | `USER abcdefghijKLMN 0 * :A` | accepted, **truncated to `abcdefghij`** | ✅ cap applies |
| 5 | `USER a~b#c 0 * :A` | accepted | ✅ punctuation is legal |
| 6 | `USER ~alice 0 * :A` | accepted | ✅ what real clients send |
| 6b | `USER a:b 0 * :A` | **accepted** | ✅ `:` is 0x3A — inside `%x21-3F` |
| 6c | `USER a!b 0 * :A` | **accepted** | ✅ `!` is 0x21 — inside the production |
| 7 | `USER a@b 0 * :A` | **461** invalid username | ❌ `@` breaks the prefix |
| 8 | `USER "" 0 * :A` | **461** | ❌ empty |
| 9 | `USER a b 0 * :A` | parse error / 461 | ❌ space splits the parameter |
| 10 | `USER a 0 * :A` twice | **462** ALREADYREGISTRED | ❌ re-registration |

```bash
# 4 — prove the truncation, by reading the prefix back
reg 6667 trunc0 & sleep 0.3
irc 6667 "PASS pass" "NICK tr" "USER abcdefghijKLMN 0 * :T" "JOIN #p" \
  | grep -oE ':tr![^ ]+'        # -> :tr!abcdefghij@127.0.0.1
# 7 — the @ rejection
irc 6667 "PASS pass" "NICK at" "USER a@b 0 * :A"   # 461
```

#### 5.3.2 `<mode>` — the bitmask matrix

RFC 2812 §3.1.3 defines this parameter as a **numeric bitmask** applied at
registration:

| Bit | Value | Meaning |
| --- | --- | --- |
| 2 | `4` | set user mode `+w` (receive wallops) |
| 3 | `8` | set user mode `+i` (invisible) |

So `0`, `4`, `8`, `12` are the meaningful values. **This server ignores the
value entirely** — it only requires a syntactically valid `middle` token. Say
so plainly; the RFC permits it, and inventing user modes the server does not
implement would be worse.

| # | mode token | Bits | Expected | Why |
| --- | --- | --- | --- | --- |
| 1 | `0` | none | accepted | ✅ the conventional value |
| 2 | `4` | +w | accepted, ignored | ✅ wallops not implemented |
| 3 | `8` | +i | accepted, ignored | ✅ invisible not implemented |
| 4 | `12` | +w+i | accepted, ignored | ✅ both bits |
| 5 | `15` | all | accepted, ignored | ✅ every low bit |
| 6 | `*` | — | accepted | ✅ what some clients send instead |
| 7 | `abc` | — | accepted, ignored | ✅ valid `middle`; not a number, but never parsed |
| 8 | `99999999999999` | — | accepted, ignored | ✅ no overflow: never converted |
| 9 | `-1` | — | accepted, ignored | ✅ same |
| 10 | *(omitted)* — `USER a 0 :A` | **461** | ❌ arity: 4 params required |
| 11 | `:0` | parse error / 461 | ❌ `middle` may not begin with `:` |

```bash
for m in 0 4 8 12 15 '*' abc 99999999999999 -1; do
  printf 'mode=%-16s -> ' "$m"
  irc 6667 "PASS pass" "NICK m$RANDOM" "USER u $m * :R" | grep -oE ' 00[14] ' | head -1
done
irc 6667 "PASS pass" "NICK short" "USER u 0 :R"     # 461 — only 3 params
```

**Row 8 is the interesting one to volunteer.** A server that ran `atoi()` on
this parameter could overflow; this one never converts it, so there is nothing
to overflow. That is a deliberate consequence of ignoring the value.

#### 5.3.3 `<unused>` matrix

RFC 2812 lists this parameter as literally unused — historically it was the
server name in a server-to-server `USER`. Clients conventionally send `*`.

| # | unused token | Expected | Why |
| --- | --- | --- | --- |
| 1 | `*` | accepted | ✅ the convention |
| 2 | `0` | accepted | ✅ any `middle` |
| 3 | `127.0.0.1` | accepted | ✅ what the field once meant |
| 4 | `irc.example.org` | accepted | ✅ likewise |
| 5 | `*.*` | accepted | ✅ no glob interpretation |
| 6 | `anything` | accepted | ✅ never read |
| 7 | `..` | accepted | ✅ not a path — no traversal risk, it is discarded |
| 8 | `%s%s%n` | accepted, echoed nowhere | ✅ **no format-string bug** |
| 9 | *(omitted)* | **461** | ❌ arity |
| 10 | `:*` | parse error / 461 | ❌ `middle` cannot start with `:` |

```bash
for u in '*' 0 127.0.0.1 irc.example.org '*.*' anything '..' '%s%s%n'; do
  printf 'unused=%-18s -> ' "$u"
  irc 6667 "PASS pass" "NICK u$RANDOM" "USER u 0 "$u" :R" | grep -oE ' 001 ' | head -1
done
```

**Row 8 deserves a sentence at the defense:** the token is never passed to a
`printf`-family function, so `%n` is inert. Demonstrating that you *considered*
format strings scores better than the test passing silently.

#### 5.3.4 `<realname>` matrix

| # | realname | Expected | Why |
| --- | --- | --- | --- |
| 1 | `:Alice Liddell` | accepted, spaces kept | ✅ trailing param |
| 2 | `:A` | accepted | ✅ 1 char |
| 3 | `:` (empty) | accepted, empty realname | ✅ legal |
| 4 | `:with :colons: inside` | kept verbatim | ✅ only the first `:` is syntax |
| 5 | `:tab\there` | accepted | ✅ tab is not a delimiter |
| 6 | `:` + 400 chars | accepted, line capped at 512 | ✅ truncation, not crash |
| 7 | `Alice` (**no** colon) | parse error → **461** | ❌ `:` is mandatory |
| 8 | `:Alice\r\nJOIN #x` | the `\r\n` ends the line; `JOIN` is a **separate command** | ⚠️ see below |
| 9 | missing entirely | **461** | ❌ arity |
| 10 | `:%n%n%n` | stored verbatim | ✅ no format-string bug |

**Row 8 is a security point worth raising yourself.** An embedded `\r\n` cannot
smuggle a hidden command *inside* a parameter — the line framer splits on it
first, so it becomes an ordinary second command subject to the same checks.
Prove it:

```bash
printf 'PASS pass\r\nNICK inj\r\nUSER u 0 * :A\r\nJOIN #injected\r\n' | nc -q1 -C 127.0.0.1 6667
# JOIN #injected runs as a normal command — no privilege gained
```

### 5.4 Registration-order matrix

| # | Order | Expected |
| --- | --- | --- |
| 1 | PASS → NICK → USER | ✅ 001 |
| 2 | PASS → USER → NICK | ✅ 001 — either order after PASS |
| 3 | NICK → USER → PASS | ❌ 464, closed |
| 4 | NICK → PASS → USER | ❌ 464 |
| 5 | PASS → NICK only | no 001, half-registered, no crash |
| 6 | PASS → USER only | no 001, half-registered |
| 7 | PASS → NICK → USER → USER | ❌ 462 on the second |
| 8 | PASS → NICK → NICK → USER | ✅ 001 with the second nick |
| 9 | JOIN before completing | ❌ 451 |
| 10 | QUIT while half-registered | clean close, no leak |

```bash
irc 6667 "PASS pass" "USER u 0 * :U" "NICK swapped"    # 2 — expect 001
irc 6667 "PASS pass" "NICK only"                       # 5 — no 001, then clean EOF
irc 6667 "PASS pass" "NICK a" "NICK b" "USER u 0 * :U" | grep -oE '001 [a-z]+'  # 8 -> 001 b
```

---

## 6. PRIVMSG matrix

> *"Verify that private messages (PRIVMSG) are fully functional with different
> parameters."*

Grammar: `privmsg-cmd = "PRIVMSG" SPACE $msgtarget SPACE [ ":" ] $msgtext *SPACE`

| # | Command | Expected | Why |
| --- | --- | --- | --- |
| 1 | `PRIVMSG bob :hello` | bob gets `:alice!… PRIVMSG bob :hello` | ✅ user target |
| 2 | `PRIVMSG #eval :hello` | every member **except** the sender | ✅ channel target |
| 3 | `PRIVMSG bob hello` | delivered (no colon needed for one word) | ✅ `:` optional |
| 4 | `PRIVMSG bob :hi there you` | spaces preserved | ✅ trailing |
| 5 | `PRIVMSG BOB :hi` | delivered to `bob` | ✅ ASCII casemapping |
| 6 | `PRIVMSG nosuch :hi` | **401** NOSUCHNICK | ❌ unknown target |
| 7 | `PRIVMSG #nosuch :hi` | **401** or **403** | ❌ unknown channel |
| 8 | `PRIVMSG` | **411** NORECIPIENT | ❌ no target |
| 9 | `PRIVMSG bob` | **412** NOTEXTTOSEND | ❌ no text |
| 10 | `PRIVMSG #eval :` (empty text) | **412** | ❌ empty is not text |
| 11 | `PRIVMSG #eval :` + 500 chars | truncated to fit 512 total | ✅ no overflow |
| 12 | `PRIVMSG alice :self` | alice **does** receive it | ✅ self-message is legal |
| 13 | `PRIVMSG #nomember :hi` (not joined) | **404** CANNOTSENDTOCHAN | ❌ if the channel exists |

```bash
irc 6667 "PASS pass" "NICK p" "USER p 0 * :P" "PRIVMSG"            # 411
irc 6667 "PASS pass" "NICK p" "USER p 0 * :P" "PRIVMSG bob"        # 412
irc 6667 "PASS pass" "NICK p" "USER p 0 * :P" "PRIVMSG ghost :hi"  # 401
# 11 — oversize payload
python3 -c "
import sys
sys.stdout.write('PASS pass\r\nNICK big\r\nUSER b 0 * :B\r\nJOIN #eval\r\nPRIVMSG #eval :' + 'X'*500 + '\r\n')" \
  | nc -q1 -C 127.0.0.1 6667 | tail -2
```

**NOTICE mirrors PRIVMSG with one rule:** a NOTICE must **never** generate an
automatic error reply, to prevent loops between bots.

```bash
irc 6667 "PASS pass" "NICK n" "USER n 0 * :N" "NOTICE ghost :hi"   # expect: SILENCE, no 401
```

---

## 7. Channel operator commands

> *"Check that a regular user does not have privileges to do channel operator
> actions. Then test with an operator. Remove one point for each feature that
> is not working."*

Set up two clients: **alice** creates `#ops` (and is therefore its first
operator), **bob** joins as a plain member.

```bash
# T2
PASS pass
NICK alice
USER alice 0 * :A
JOIN #ops
# T3
PASS pass
NICK bob
USER bob 0 * :B
JOIN #ops
```

Confirm the operator prefix in the NAMES reply — `@alice`, plain `bob`:

```
:ft_irc 353 alice = #ops :@alice bob
:ft_irc 366 alice #ops :End of /NAMES list
```

### 7.1 The privilege matrix — run every row twice, as bob then as alice

| # | Command | As **bob** (regular) | As **alice** (operator) |
| --- | --- | --- | --- |
| 1 | `KICK #ops bob :bye` | **482** CHANOPRIVSNEEDED | bob removed, all see `KICK` |
| 2 | `INVITE carol #ops` | **482** *(when `+i`)* | **341** RPL_INVITING |
| 3 | `TOPIC #ops :new` | **482** *(when `+t`)* | topic set, broadcast |
| 4 | `TOPIC #ops` (read) | **332** or **331** | same — reading is never restricted |
| 5 | `MODE #ops +i` | **482** | mode set, broadcast |
| 6 | `MODE #ops +t` | **482** | mode set |
| 7 | `MODE #ops +k secret` | **482** | key set |
| 8 | `MODE #ops +l 10` | **482** | limit set |
| 9 | `MODE #ops +o bob` | **482** | bob becomes `@bob` |
| 10 | `MODE #ops` (query) | **324** — allowed | **324** |
| 11 | `MODE #ops -o alice` (self-demote) | **482** | alice loses `@` |
| 12 | `KICK #ops alice :bye` | **482** | operator may kick an operator |

```bash
# bob attempts every operator action — all must be 482
for c in "KICK #ops alice :x" "INVITE carol #ops" "TOPIC #ops :hijack" \
         "MODE #ops +i" "MODE #ops +t" "MODE #ops +k k" "MODE #ops +l 5" "MODE #ops +o bob"; do
  printf '%-26s -> ' "$c"
  reg 6667 bobtest "JOIN #ops" "$c" | grep -oE ' (482|324|341|332) ' | head -1
done
```

### 7.2 Feature-by-feature checks (one point each)

**KICK**

| # | Command | Expected |
| --- | --- | --- |
| 1 | `KICK #ops bob :reason` | `:alice!… KICK #ops bob :reason` to all |
| 2 | `KICK #ops bob` (no reason) | works, default reason |
| 3 | `KICK #ops ghost :x` | **441** USERNOTINCHANNEL |
| 4 | `KICK #nosuch bob :x` | **403** NOSUCHCHANNEL |
| 5 | `KICK #ops bob` when alice not in channel | **442** NOTONCHANNEL |
| 6 | `KICK` (no params) | **461** |
| 7 | `KICK #a,#b bob :x` | multi-channel form |
| 8 | `KICK #ops bob,carol :x` | multi-user form |

**INVITE**

| # | Command | Expected |
| --- | --- | --- |
| 1 | `INVITE carol #ops` | **341** to alice, `INVITE` to carol |
| 2 | `INVITE bob #ops` (already in) | **443** USERONCHANNEL |
| 3 | `INVITE ghost #ops` | **401** NOSUCHNICK |
| 4 | `INVITE carol #nosuch` | **403** |
| 5 | `INVITE` (no params) | **461** |
| 6 | invited user joins `+i` channel | succeeds — the invite is consumed |
| 7 | uninvited user joins `+i` channel | **473** INVITEONLYCHAN |

**TOPIC**

| # | Command | Expected |
| --- | --- | --- |
| 1 | `TOPIC #ops` with no topic set | **331** NOTOPIC |
| 2 | `TOPIC #ops :hello` | broadcast, then **332** on read |
| 3 | `TOPIC #ops` after setting | **332** + **333** (who/when) |
| 4 | `TOPIC #ops :` (empty) | topic cleared → **331** |
| 5 | `TOPIC #ops :new` as bob under `+t` | **482** |
| 6 | `TOPIC #ops :new` as bob under `-t` | allowed |
| 7 | `TOPIC #nosuch :x` | **403** |
| 8 | 400-char topic | truncated to 390 (`kTopicLen`), no crash |

```bash
# 6 — prove -t actually delegates topic control
reg 6667 alice "JOIN #t1" "MODE #t1 -t" &
sleep 0.4; reg 6667 bob "JOIN #t1" "TOPIC #t1 :bob set this"   # succeeds, no 482
```

---

## 8. The MODE matrix

This server implements exactly **five** channel modes, and they are declared to
clients in `004` and `005`:

```
:ft_irc 004 alice ft_irc 1.0 o itkol
:ft_irc 005 alice CHANTYPES=# PREFIX=(o)@ CHANMODES=,,kl,it …
```

`i t k o l` — five flags, so **2⁵ = 32** on/off combinations. Every one is
enumerated below.

### 8.1 The parameter rules — where most implementations get it wrong

Straight from `include/ChannelModes.hpp`:

| Mode | Meaning | Param when **adding** | Param when **removing** |
| --- | --- | --- | --- |
| `i` | invite-only | no | no |
| `t` | topic locked to operators | no | no |
| `k` | channel key (password) | **yes** — the key | no — *but a spare one is tolerated* |
| `o` | grant operator | **yes** — a nick | **yes** — a nick |
| `l` | user limit | **yes** — a number | no |

Two consequences worth stating before the grader finds them:

- **`+k` needs a param, `-k` does not** — but real clients send `MODE #c -k
  oldkey` anyway, so a spare parameter on `-k` is *accepted and discarded*
  (`spareParamOnRemove`). Reject it and HexChat breaks.
- **`o` is the only mode needing a param in both directions**, because you must
  always name *whom*.

This is why parameter counting is two passes in the code:
`mandatoryParams()` counts how many are required, `firstKeyParam()` then locates
*which* positional parameter is the key — needed so the trace logger can redact
it. In `MODE #c +ok bob secret`, the key is parameter index 1, not 0.

### 8.2 All 32 add-combinations

Set up once:

```bash
reg 6667 alice "JOIN #m"      # alice is operator of #m
```

Then, for any row: `MODE #m +<letters> <params…>` and read back with `MODE #m`.

| # | `itkol` | MODE command (add) | params | resulting `324` mode string |
| --- | --- | --- | --- | --- |
| 0 | `00000` | `MODE #m` | 0 | query only — `324 #m +` |
| 1 | `00001` | `MODE #m +l 10` | 1 | `+l 10` |
| 2 | `00010` | `MODE #m +o bob` | 1 | `+ (o is a privilege, not a mode)` |
| 3 | `00011` | `MODE #m +ol bob 10` | 2 | `+l 10` |
| 4 | `00100` | `MODE #m +k secret` | 1 | `+k secret` |
| 5 | `00101` | `MODE #m +kl secret 10` | 2 | `+kl secret 10` |
| 6 | `00110` | `MODE #m +ko secret bob` | 2 | `+k secret` |
| 7 | `00111` | `MODE #m +kol secret bob 10` | 3 | `+kl secret 10` |
| 8 | `01000` | `MODE #m +t` | 0 | `+t` |
| 9 | `01001` | `MODE #m +tl 10` | 1 | `+tl 10` |
| 10 | `01010` | `MODE #m +to bob` | 1 | `+t` |
| 11 | `01011` | `MODE #m +tol bob 10` | 2 | `+tl 10` |
| 12 | `01100` | `MODE #m +tk secret` | 1 | `+tk secret` |
| 13 | `01101` | `MODE #m +tkl secret 10` | 2 | `+tkl secret 10` |
| 14 | `01110` | `MODE #m +tko secret bob` | 2 | `+tk secret` |
| 15 | `01111` | `MODE #m +tkol secret bob 10` | 3 | `+tkl secret 10` |
| 16 | `10000` | `MODE #m +i` | 0 | `+i` |
| 17 | `10001` | `MODE #m +il 10` | 1 | `+il 10` |
| 18 | `10010` | `MODE #m +io bob` | 1 | `+i` |
| 19 | `10011` | `MODE #m +iol bob 10` | 2 | `+il 10` |
| 20 | `10100` | `MODE #m +ik secret` | 1 | `+ik secret` |
| 21 | `10101` | `MODE #m +ikl secret 10` | 2 | `+ikl secret 10` |
| 22 | `10110` | `MODE #m +iko secret bob` | 2 | `+ik secret` |
| 23 | `10111` | `MODE #m +ikol secret bob 10` | 3 | `+ikl secret 10` |
| 24 | `11000` | `MODE #m +it` | 0 | `+it` |
| 25 | `11001` | `MODE #m +itl 10` | 1 | `+itl 10` |
| 26 | `11010` | `MODE #m +ito bob` | 1 | `+it` |
| 27 | `11011` | `MODE #m +itol bob 10` | 2 | `+itl 10` |
| 28 | `11100` | `MODE #m +itk secret` | 1 | `+itk secret` |
| 29 | `11101` | `MODE #m +itkl secret 10` | 2 | `+itkl secret 10` |
| 30 | `11110` | `MODE #m +itko secret bob` | 2 | `+itk secret` |
| 31 | `11111` | `MODE #m +itkol secret bob 10` | 3 | `+itkl secret 10` |

Note rows where `o` appears: `o` is a **member privilege**, not a channel flag,
so it never shows up in the `324` mode string. It shows as the `@` prefix in
`353 NAMES` instead. Graders ask about this — have the answer ready.

```bash
# sweep every one of the 32 combinations automatically
alias|grep -q irc || . /dev/stdin <<< "$(declare -f irc reg)"
for n in $(seq 0 31); do
  letters=""; params=""
  for i in 0 1 2 3 4; do
    if (( (n >> (4-i)) & 1 )); then
      c=$(echo "i t k o l" | cut -d' ' -f$((i+1)))
      letters+="$c"
      case $c in k) params+=" secret";; o) params+=" alice";; l) params+=" 10";; esac
    fi
  done
  [ -z "$letters" ] && continue
  printf '%2d  +%-6s -> ' "$n" "$letters"
  reg 6667 "s$n" "JOIN #m$n" "MODE #m$n +$letters$params" "MODE #m$n" \
    | grep -oE ' 324 [^\r]*' | tail -1
done
```

### 8.3 Arity failures — 10 that must be refused

| # | Command | Expected | Why |
| --- | --- | --- | --- |
| 1 | `MODE #m +k` | **461** NEEDMOREPARAMS | `+k` needs a key |
| 2 | `MODE #m +o` | **461** | `+o` needs a nick |
| 3 | `MODE #m +l` | **461** | `+l` needs a number |
| 4 | `MODE #m +kl secret` | **461** | needs 2 params, got 1 |
| 5 | `MODE #m +okl bob` | **461** | needs 3, got 1 |
| 6 | `MODE #m -o` | **461** | `-o` also needs a nick |
| 7 | `MODE #m +l abc` | **461** / **696** | limit must be numeric |
| 8 | `MODE #m +l -5` | **461** / **696** | negative limit |
| 9 | `MODE #m +o ghost` | **401** / **441** | no such user in channel |
| 10 | `MODE #m +z` | **472** UNKNOWNMODE | `z` is not implemented |
| 11 | `MODE` | **461** | no target |
| 12 | `MODE #nosuch +i` | **403** NOSUCHCHANNEL | unknown channel |

```bash
for c in "MODE #m +k" "MODE #m +o" "MODE #m +l" "MODE #m +kl secret" "MODE #m +okl bob" \
         "MODE #m -o" "MODE #m +l abc" "MODE #m +l -5" "MODE #m +z" "MODE" "MODE #nosuch +i"; do
  printf '%-24s -> ' "$c"
  reg 6667 "a$RANDOM" "JOIN #m" "$c" | grep -oE ' (461|472|403|401|441|696|324) ' | tail -1
done
```

### 8.4 The mode string is a language of its own

`MODE <target> <modes> [params…]` — the `<modes>` token has a grammar, and the
32-row table above only exercises the pure-add corner of it. Two rules govern
the rest.

#### Rule 1 — it must open with a sign

There is **no implicit `+`**. A string that does not begin with `+` or `-`
applies nothing *and is answered with nothing* — **not even 472**, because its
characters were never mode characters in the first place.

| # | Command | Expected | Why |
| --- | --- | --- | --- |
| 1 | `MODE #c +i` | `+i` set, broadcast | ✅ opens with a sign |
| 2 | `MODE #c -o ali` | `-o` applied | ✅ |
| 3 | `MODE #c -o+i-t ali` | all three applied in order | ✅ signs may flip |
| 4 | `MODE #c i` | **silence** — no change, **no 472** | ❌ no sign, so `i` is not a mode char |
| 5 | `MODE #c it` | silence | ❌ same |
| 6 | `MODE #c o ali` | silence | ❌ same |
| 7 | `MODE #c +` | silence — a lone sign applies nothing | ✅ well-formed, empty |
| 8 | `MODE #c -` | silence | ✅ |
| 9 | `MODE #c +-+-i` | applies **`-i`** | ✅ a run of signs collapses to the last one before the letter |
| 10 | `MODE #c ++i` | applies `+i` | ✅ same collapse |

```bash
# rows 4-9, verified: signless applies nothing and does NOT emit 472
for m in "+i" "i" "it" "o ali" "+" "-" "+-+-i" "++i"; do
  printf '  MODE %-10s -> ' "$m"
  reg 6667 "s$RANDOM" "JOIN #sg" "MODE #sg $m" | grep -oE 'MODE #sg [^\r]*| 47[23] ' | tail -1
done
```

**Row 4 is the one to volunteer.** Emitting `472 ERR_UNKNOWNMODE` for `MODE #c
i` is a common bug: `i` *is* a known mode, it simply is not a mode *character*
in that position, because nothing has established a sign. Silence is correct.

#### Rule 2 — authorisation is answered before the string is parsed

`403`, `442` and `482` come first. They answer *"may you touch this target's
modes at all?"* — true or false regardless of what the string turns out to say.

| # | Situation | Command | Expected |
| --- | --- | --- | --- |
| 1 | channel does not exist | `MODE #ghost +zzz` | **403**, not 472 |
| 2 | not a member | `MODE #other +zzz` | **442**, not 472 |
| 3 | member, not operator | `MODE #c +zzz` | **482**, not 472 |
| 4 | operator | `MODE #c +zzz` | **472** ×1 (only now) |

### 8.5 Mixed-sign cumulative matrix

Past the opening sign, letters cumulate and the sign may flip mid-string. Each
parameter-taking letter draws the **next** positional parameter, in the order
the letters appear.

| # | Command | Params drawn | Expected echo |
| --- | --- | --- | --- |
| 1 | `MODE #c +i-o+lk ali 5 secret` | `o`←ali, `l`←5, `k`←secret | `+i-o+lk ali 5 secret` |
| 2 | `MODE #c -o+i-t ali` | `o`←ali | `-o+i-t ali` |
| 3 | `MODE #c +ikl secret 5` | `k`←secret, `l`←5 | `+ikl secret 5` |
| 4 | `MODE #c +ko secret ali` | `k`←secret, `o`←ali | `+ko secret ali` |
| 5 | `MODE #c +ok ali secret` | `o`←ali, `k`←secret | `+ok ali secret` |
| 6 | `MODE #c +ii` | none | `i` applied twice, idempotent |
| 7 | `MODE #c +it` | none | `+it` |
| 8 | `MODE #c -oo ali bob` | `o`←ali, `o`←bob | both revoked |
| 9 | `MODE #c -oi ali` | `o`←ali | `-oi ali` — one sign, two letters |
| 10 | `MODE #c +i-i` | none | set then cleared, net no-op |
| 11 | `MODE #c -k+o ali` | **`o`←ali** | `-k` takes none here — see below |
| 12 | `MODE #c +o-k ali` | `o`←ali | 1 param, not 2 |
| 13 | `MODE #c +t-l` | none | `-l` takes no param |
| 14 | `MODE #c +l-l 5` | `l`←5 | set then unset |
| 15 | `MODE #c -i-t-k-l` | none | four removals, zero params |

**Rows 11 and 12 are the trap.** `-k` takes the key **only when the modes still
to come do not need it**. In `-k+o ali`, the pending `+o` needs `ali`, so `-k`
must not eat it. An implementation that counts parameters by letter alone,
ignoring sign and lookahead, gives `ali` to `-k` and then emits a spurious
**461** for `+o`.

```bash
# row 1 — your mixed-sign case, verified verbatim
reg 6667 ali "JOIN #mx" "MODE #mx +i-o+lk ali 5 secret"
# echo: :ali!ali@127.0.0.1 MODE #mx +i-o+lk ali 5 secret

# rows 11/12 — must NOT be 461
reg 6667 ali "JOIN #k1" "MODE #k1 -k+o ali"
reg 6667 ali "JOIN #k2" "MODE #k2 +o-k ali"
```

### 8.6 Error de-duplication — once per *distinct* complaint

Errors are reported once per distinct complaint, **not** once per occurrence.

| # | Command | Expected count | Why |
| --- | --- | --- | --- |
| 1 | `MODE #c +ooo` (no params) | **one** 461 | one complaint: "not enough parameters" |
| 2 | `MODE #c +jfsadfsahf` | **six** 472 | 10 letters, 6 distinct: `j f s a d h` |
| 3 | `MODE #c +jj` | one 472 for `j` | duplicate letter, one complaint |
| 4 | `MODE #c +zzz+zzz` | one 472 for `z` | signs do not reset the set |
| 5 | `MODE #c +okl` (no params) | one 461 | not three |

```bash
reg 6667 d1 "JOIN #d1" "MODE #d1 +jfsadfsahf" | grep -c 472      # -> 6
reg 6667 d2 "JOIN #d2" "MODE #d2 +ooo"        | grep -c 461      # -> 1
```

Verified: `+jfsadfsahf` returns exactly **6** `472` replies, for `j f s a d h`.
A server that answers 10 is echoing occurrences, not complaints.

### 8.7 The echo, and the 512-octet split

The broadcast echo restates the sign **only where it actually changes**, so
`+o-i-o a b` comes back as `+o-io a b` — the second `-` is redundant and is
dropped. A mode string dense enough that its echo would exceed 512 octets is
**split across several `MODE` lines** rather than truncated, because a
truncated mode line would misrepresent what was applied.

| # | Command | Expected echo |
| --- | --- | --- |
| 1 | `MODE #c +o-i-o a b` | `+o-io a b` — signs coalesced |
| 2 | `MODE #c +i+t` | `+it` |
| 3 | `MODE #c -i-t` | `-it` |
| 4 | `MODE #c +i-t+k s` | `+i-t+k s` — every sign here is a real change |
| 5 | 60 × `+o <nick>` in one string | two or more `MODE` lines, none over 512 |

These two are exercised by `ModeMatrixTest` in `tests/test_modes.cpp` — notably
`ErrorsAreReportedOncePerDistinctComplaint`. Run it in front of the grader:

```bash
make test 2>&1 | grep -i modematrix | tail -20
```

### 8.8 Behaviour of each mode, end to end

| Mode | Set it | Then verify | Expected |
| --- | --- | --- | --- |
| `+i` | `MODE #m +i` | outsider `JOIN #m` | **473** INVITEONLYCHAN |
| `+i` | + `INVITE bob #m` | bob `JOIN #m` | succeeds |
| `+t` | `MODE #m +t` | non-op `TOPIC #m :x` | **482** |
| `+k` | `MODE #m +k secret` | `JOIN #m` no key | **475** BADCHANNELKEY |
| `+k` | | `JOIN #m secret` | succeeds |
| `+k` | | `JOIN #m wrong` | **475** |
| `+l` | `MODE #m +l 1` | 2nd user joins | **471** CHANNELISFULL |
| `+o` | `MODE #m +o bob` | `NAMES #m` | `@bob` |
| `-o` | `MODE #m -o bob` | `NAMES #m` | plain `bob` |

```bash
# +k end to end
reg 6667 kop "JOIN #kc" "MODE #kc +k secret" & sleep 0.4
irc 6667 "PASS pass" "NICK nokey" "USER n 0 * :N" "JOIN #kc"          # 475
irc 6667 "PASS pass" "NICK haskey" "USER h 0 * :H" "JOIN #kc secret"  # succeeds

# +l end to end
reg 6667 lop "JOIN #lc" "MODE #lc +l 1" & sleep 0.4
irc 6667 "PASS pass" "NICK second" "USER s 0 * :S" "JOIN #lc"         # 471
```

**Bonus point to volunteer:** channel keys are redacted from the server's own
trace log. Show it:

```bash
FT_IRC_LOG=trace ./build/bin/ircserv 6667 pass 2>&1 | grep 'MODE' &
reg 6667 red "JOIN #r" "MODE #r +k topsecret"
# trace shows:  MODE #r +k ***    — the key never reaches the log
```

---

## 9. Grammar torture

> *"No segfault, no other unexpected, premature, uncontrolled or unexpected
> termination of the program, else the final grade is 0."*

This server matches every line against the RFC 2812 ABNF grammar before
dispatching it, so malformed input is rejected by the parser rather than by
scattered hand-written checks. That is the claim — these tables are how you
prove it.

### 9.0 The message signature

Every line is matched against this, from RFC 2812 §2.3.1:

```abnf
message    =  [ ":" prefix SPACE ] command [ params ] crlf
prefix     =  servername / ( nickname [ [ "!" user ] "@" host ] )
command    =  1*letter / 3digit
params     =  *14( SPACE middle ) [ SPACE ":" trailing ]
           =/ 14( SPACE middle ) [ SPACE [ ":" ] trailing ]
nospcrlfcl =  %x01-09 / %x0B-0C / %x0E-1F / %x21-39 / %x3B-FF
middle     =  nospcrlfcl *( ":" / nospcrlfcl )
trailing   =  *( ":" / " " / nospcrlfcl )
SPACE      =  %x20
crlf       =  %x0D %x0A
```

Four consequences a grader can probe, each with its own control point.

**A. `command` is `1*letter` or exactly `3digit`.** Nothing else is a command
shape — not `12`, not `1234`, not `A1`.

| # | Line | Expected | Why |
| --- | --- | --- | --- |
| 1 | `PING :x` | `PONG` | ✅ `1*letter` |
| 2 | `ping :x` | `PONG` | ✅ commands are case-insensitive |
| 3 | `PINGPINGPING` | **421** | ✅ valid *shape*, unknown *name* |
| 4 | `421` | silence | ✅ `3digit` is a valid shape, but numerics are server→client only |
| 5 | `12` | **silence** — no 421 | ❌ two digits matches neither production |
| 6 | `1234` | **silence** | ❌ four digits |
| 7 | `A1` | **silence** | ❌ letter+digit is neither |
| 8 | `PING1` | **silence** | ❌ same |
| 9 | `+PING` | **silence** | ❌ `+` is not a letter |
| 10 | *(empty)* | silence | ✅ RFC: ignore empty messages |

**Rows 5–9 are the point of this table, and they are easy to get backwards.**
`421 ERR_UNKNOWNCOMMAND` is *not* the answer here. 421 means "that is a command
name, and I do not implement it" — which requires the token to *be* a command
name. `12` never matches `1*letter / 3digit`, so the whole **message** fails to
parse, and an unparseable message is ignored exactly like an empty one.

Compare row 3: `PINGPINGPING` **is** `1*letter`, so it is a command name, so it
gets 421.

This is the same principle as the MODE signless rule in [§8.4](#84-the-mode-string-is-a-language-of-its-own):
a token that never matched the production was never the thing being named, so
there is nothing to report as unknown. Being able to state that symmetry is
worth a mark.

```bash
for c in "PING :x" "PINGPINGPING" "12" "1234" "A1" "PING1" "+PING"; do
  printf '  %-14s -> ' "$c"
  irc 6667 "PASS pass" "NICK s$RANDOM" "USER u 0 * :U" "$c" \
    | grep -oE ' 421 |PONG' | tail -1 || echo "silence"
done
```

**B. `middle` may contain `:` — just not as its first octet.** `nospcrlfcl`
excludes `:` (0x3A), but `middle = nospcrlfcl *( ":" / nospcrlfcl )` re-admits
it from the second octet on.

| # | Line | Expected | Why |
| --- | --- | --- | --- |
| 1 | `PRIVMSG a:b :hi` | target `a:b` | ✅ `:` legal past position 0 |
| 2 | `PRIVMSG :ab :hi` | `:ab` opens the trailing | ❌ different parse entirely |
| 3 | `USER a:b 0 * :R` | accepted | ✅ verified |
| 4 | `MODE #a:b +i` | channel `#a:b` | ✅ |

**C. The 15th parameter's colon is optional.** That is what the `=/` line
means: after exactly 14 `middle`s, the trailing may be introduced with **or
without** `:`. Under 14, the colon is mandatory to start a trailing.

| # | Parameters | Colon on the last? | Expected |
| --- | --- | --- | --- |
| 1 | 3 middles + `:trailing` | required | ✅ parsed as trailing |
| 2 | 3 middles + bare word | — | ✅ it is a 4th middle, not a trailing |
| 3 | exactly 14 middles + `:trailing` | present | ✅ |
| 4 | exactly 14 middles + bare trailing | **absent** | ✅ still a trailing — the `=/` rule |
| 5 | 15 middles + `:trailing` | — | ❌ over the cap; the 15th absorbs the rest |

```bash
# 4 — 14 middles then a colon-less trailing with spaces in it
python3 -c "
mid=' '.join('p%d'%i for i in range(14))
print('PRIVMSG #g ' + mid + ' tail with spaces')" | head -1
```

**D. `trailing` may contain spaces and colons; it ends only at CRLF.**

| # | Line | Realname / text stored |
| --- | --- | --- |
| 1 | `USER a 0 * :A B C` | `A B C` |
| 2 | `USER a 0 * :a:b:c` | `a:b:c` |
| 3 | `USER a 0 * :` | *(empty)* |
| 4 | `USER a 0 * :  lead+trail  ` | verbatim, whitespace kept |
| 5 | `USER a 0 * :A<CR>JOIN #x` | `A` — the CR **terminates the line**; `JOIN #x` is a separate command |

Row 5 is the injection question. An embedded CR or LF cannot smuggle a command
*inside* a parameter, because the framer splits on it before the parser ever
runs. The smuggled text becomes an ordinary next command, subject to every
normal check. Prove it rather than assert it:

```bash
printf 'PASS pass\r\nNICK inj\r\nUSER u 0 * :A\r\nJOIN #injected\r\n' | nc -q1 -C 127.0.0.1 6667
```

**E. `prefix` from a client is accepted and ignored.** `prefix = servername /
( nickname [ [ "!" user ] "@" host ] )`. A client that sends one cannot use it
to impersonate anybody — the server stamps its own.

| # | Line | Expected |
| --- | --- | --- |
| 1 | `:nick PING :x` | PONG — prefix ignored |
| 2 | `:nick!user@host PING :x` | PONG |
| 3 | `:other PRIVMSG #c :spoof` | relayed as **the real sender**, not `other` |
| 4 | `: PING :x` | **421** or ignored — empty prefix |
| 5 | `:only-a-prefix` | ignored, no command |

```bash
# 3 — the impersonation attempt must fail
reg 6667 mallory "JOIN #spoof" ":alice PRIVMSG #spoof :I am alice"
# other members see :mallory!...  — never :alice
```

### 9.1 Message-framing table

| # | Input (before CRLF) | Expected | Why |
| --- | --- | --- | --- |
| 1 | `PING :tok` | `PONG :tok` | ✅ nominal |
| 2 | `ping :tok` | `PONG` | ✅ commands are case-insensitive |
| 3 | `PING` | **409** NOORIGIN | ❌ PING needs an origin |
| 4 | *(empty line)* | silently ignored | ✅ RFC: empty messages are ignored |
| 5 | `   ` (spaces only) | ignored or **421** | ✅ no crash |
| 6 | `:prefix PING :tok` | accepted, prefix ignored | ✅ clients may send one |
| 7 | `NOTACOMMAND` | **421** UNKNOWNCOMMAND | ❌ unknown |
| 8 | `123` | **421** or ignored | ⚠️ `3digit` is a valid *command* shape |
| 9 | `12` | **421** | ❌ neither `1*letter` nor `3digit` |
| 10 | `PING\|extra` | **421** | ❌ `\|` is not a letter |
| 11 | 16 parameters | 15th absorbs the rest | ✅ `*14( middle )` + trailing |
| 12 | 512-byte line | accepted | ✅ exactly at the limit |
| 13 | 513-byte line | truncated at 512 | ✅ **and the tail is discarded** |

**Row 13 is a security property, not a rounding detail.** `LineBuffer`
truncates an over-long line *and throws away everything up to its terminator*.
Without that, a peer could push a command past the limit by prefixing it with
512 bytes of padding and have the parser execute the remainder. Demonstrate:

```bash
python3 -c "
import sys
sys.stdout.write('PASS pass\r\nNICK ov\r\nUSER o 0 * :O\r\n')
sys.stdout.write('PRIVMSG #x :' + 'A'*600 + '\r\nJOIN #smuggled\r\n')" \
  | nc -q1 -C 127.0.0.1 6667
# JOIN #smuggled must run as its own command — NOT as the tail of the long line
```

### 9.2 Octet-level abuse — 10 that must not crash

| # | Payload | Expected |
| --- | --- | --- |
| 1 | embedded NUL: `NICK a\0b` | stripped or rejected, no crash |
| 2 | bare `\r` with no `\n` | buffered until a real terminator |
| 3 | bare `\n` with no `\r` | accepted (lenient framing) |
| 4 | `\n\r` reversed | treated as two terminators |
| 5 | high-bit octets `\xff\xfe` in a nick | **432** |
| 6 | UTF-8 `é` in a realname | accepted, stored verbatim |
| 7 | 8 KiB single line | truncated at 512, no realloc storm |
| 8 | 1 MiB with no terminator | connection capped/dropped, no OOM |
| 9 | `:` alone | ignored / **421** |
| 10 | `PRIVMSG #c :` + 400 × `%s` | delivered verbatim, no format bug |

```bash
# 1 — NUL injection
printf 'PASS pass\r\nNICK a\x00b\r\nUSER u 0 * :U\r\n' | nc -q1 127.0.0.1 6667
# 5 — high-bit nick
printf 'PASS pass\r\nNICK \xff\xfe\r\nUSER u 0 * :U\r\n' | nc -q1 127.0.0.1 6667
# 8 — a megabyte with no CRLF
python3 -c "import sys; sys.stdout.write('A'*1048576)" | nc -q2 127.0.0.1 6667
ps -o rss= -p $(pgrep ircserv)     # RSS must not balloon
```

**After every single one of these, the server must still be alive:**

```bash
pgrep ircserv >/dev/null && echo "  ok  still running" || echo "  CRASHED — grade 0"
reg 6667 alive "JOIN #eval" "PRIVMSG #eval :still here"
```

### 9.3 The project's own fuzz corpus

There is already an automated version of this section — run it in front of the
grader:

```bash
make test 2>&1 | tail -3          # 666 assertions, includes a fuzz pass
ls tests/grammar/                 # corpus.py — the generator
```

---

## 10. Stress & memory

> *"You must also verify the absence of memory leaks. Any memory allocated on
> the heap must be properly freed before the end of execution."*

### 10.1 Valgrind, clean shutdown

The server must exit through its normal path, not by `kill -9`, or valgrind
reports the world as leaked.

```bash
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
         --error-exitcode=42 ./build/bin/ircserv 6667 pass 2>&1 | tee /tmp/vg.log &
sleep 2
for i in $(seq 1 20); do reg 6667 "v$i" "JOIN #vg" "PRIVMSG #vg :m$i" "QUIT :bye"; done
kill -INT $(pgrep -f 'ircserv 6667')      # SIGINT -> clean shutdown path
wait
grep -E 'definitely lost|indirectly lost|ERROR SUMMARY' /tmp/vg.log
```

**Expected:**

```
definitely lost: 0 bytes in 0 blocks
indirectly lost: 0 bytes in 0 blocks
ERROR SUMMARY: 0 errors from 0 contexts
```

`still reachable` from static initialisation is acceptable and worth explaining
if it appears; `definitely lost` is not.

There is a wrapper script already:

```bash
bash scripts/memcheck.sh
```

### 10.2 Leaks specifically during the ^Z flood (§4.4)

The sheet asks for this explicitly. Run the freeze-and-flood under valgrind:

```bash
valgrind --leak-check=full ./build/bin/ircserv 6667 pass 2>&1 | tee /tmp/vg-flood.log &
sleep 2
nc -C 127.0.0.1 6667 <<< $'PASS pass\r\nNICK slow\r\nUSER s 0 * :S\r\nJOIN #f\r\n' &
NCPID=$!; sleep 1; kill -STOP $NCPID              # the ^Z
for i in $(seq 1 3000); do printf 'PRIVMSG #f :flood %d\r\n' $i; done \
  | nc -q2 -C 127.0.0.1 6667
kill -CONT $NCPID; sleep 2; kill $NCPID
kill -INT $(pgrep -f 'ircserv 6667'); wait
grep 'definitely lost' /tmp/vg-flood.log
```

### 10.3 Connection churn

```bash
# 200 connect/register/quit cycles — fd leaks show up here
for i in $(seq 1 200); do reg 6667 "c$i" "QUIT :bye" >/dev/null 2>&1; done
ls /proc/$(pgrep ircserv)/fd | wc -l     # must return to its baseline (~5)
```

A climbing fd count means sockets are not being closed. Check the baseline
before the loop so you can compare.

### 10.4 Concurrency ceiling

```bash
for i in $(seq 1 100); do ( reg 6667 "load$i" "JOIN #big" "PRIVMSG #big :hi" ) & done
wait
ss -tn state established '( sport = :6667 )' | wc -l
top -b -n1 -p $(pgrep ircserv) | tail -2      # CPU must settle back to ~0%
```

`Limits::kMaxClients` is 1024; past it, new connections are refused cleanly with
a log line rather than a crash.

### 10.5 The full pre-defense sweep

```bash
make re && make test && bash scripts/audit.sh && make norm && bash scripts/memcheck.sh
```

All five must be green. Run this the night before, not during the defense.

---

## 11. Bonus — file transfer

> *"Evaluate the bonus part if, and only if, the mandatory part has been
> entirely and perfectly done… File transfer works with the reference IRC
> client."*

Do not open this section unless everything above passed.

```bash
make bonus                    # or: make  (full tier)
```

The transfer relays base64 chunks between two connected clients over the IRC
command channel — the server never touches the filesystem and never opens a
second socket.

| Step | Sender (alice) | Recipient (bob) |
| --- | --- | --- |
| 1 | `FILE SEND bob notes.txt 12` | `FILE OFFER <id> notes.txt 12` |
| 2 | | `FILE ACCEPT <id>` |
| 3 | `FILE DATA <id> aGVsbG8gd29ybGQh` | `FILE DATA <id> aGVsbG8gd29ybGQh` |
| 4 | `FILE END <id>` | `FILE END <id> 12` |

```bash
# alice
reg 6667 alice "FILE SEND bob notes.txt 12"
# bob
reg 6667 bob "FILE ACCEPT 1"
# alice
reg 6667 alice "FILE DATA 1 aGVsbG8gd29ybGQh" "FILE END 1"
```

### 11.1 Failure matrix — 10 rejections

| # | Command | Expected | Why |
| --- | --- | --- | --- |
| 1 | `FILE SEND ghost f.txt 10` | `no such nick` | ❌ unknown recipient |
| 2 | `FILE SEND alice f.txt 10` (self) | `cannot send to yourself` | ❌ |
| 3 | `FILE SEND bob ../etc/passwd 10` | `invalid filename` | ❌ **path traversal** |
| 4 | `FILE SEND bob "a b.txt" 10` | `invalid filename` | ❌ space breaks the wire format |
| 5 | `FILE SEND bob a,b.txt 10` | `invalid filename` | ❌ comma is the list separator |
| 6 | `FILE SEND bob f.txt 0` | `invalid size` | ❌ must be ≥ 1 |
| 7 | `FILE SEND bob f.txt 99999999999` | `invalid size` | ❌ over the 50 MiB cap |
| 8 | `FILE DATA 1 not*base64!` | transfer aborted | ❌ malformed chunk |
| 9 | `FILE DATA 1 QQ` | aborted | ❌ length not a multiple of 4 |
| 10 | `FILE DATA 999 QUJD` | `no transfer with id` | ❌ unknown id |
| 11 | `FILE DATA <id>` before ACCEPT | `not accepted yet` | ❌ ordering |
| 12 | more bytes than declared | `size overrun`, aborted | ❌ metering |

```bash
for f in "../etc/passwd" "a b.txt" "a,b.txt" "." ".."; do
  printf '%-16s -> ' "$f"; reg 6667 "ft$RANDOM" "FILE SEND bob $f 10" | grep -oE 'invalid filename|no such nick'
done
```

**Path traversal is the point to volunteer.** `../etc/passwd` is refused by
`libcpp::str::is_safe_path_component`, which rejects `.`, `..`, `/`, `\`,
control bytes and DEL — and the FILE layer adds space and comma on top because
the IRC wire format is space-delimited with comma-separated lists.

### 11.2 Idle timeout and cleanup

| # | Scenario | Expected |
| --- | --- | --- |
| 1 | offer, then wait 60 s | `FILE ABRT <id> :timeout` to both |
| 2 | recipient disconnects mid-transfer | `FILE ABRT <id> :peer disconnected` |
| 3 | sender disconnects mid-transfer | same, recipient notified |
| 4 | recipient's SendQ half full | `FILE WAIT <id>` — backpressure, not a drop |
| 5 | two transfers, same pair | `a transfer to bob is already active` |

```bash
make test 2>&1 | grep -i filetransfer      # the automated version
```

---

## Appendix A — numeric quick reference

Every numeric this server can emit, from `include/ReplyList.hpp`:

| Code | Name | Triggered by |
| --- | --- | --- |
| 001–005 | WELCOME / YOURHOST / CREATED / MYINFO / ISUPPORT | registration completes |
| 221 | UMODEIS | `MODE <nick>` |
| 311/312/318/319 | WHOIS replies | `WHOIS` |
| 315/352 | WHO replies | `WHO` |
| 324/329 | CHANNELMODEIS / CREATIONTIME | `MODE #c` |
| 331/332/333 | NOTOPIC / TOPIC / TOPICWHOTIME | `TOPIC` |
| 341 | INVITING | `INVITE` |
| 353/366 | NAMREPLY / ENDOFNAMES | `JOIN`, `NAMES` |
| 401 | NOSUCHNICK | unknown target |
| 403 | NOSUCHCHANNEL | unknown channel |
| 404 | CANNOTSENDTOCHAN | not a member |
| 409 | NOORIGIN | `PING` with no token |
| 411/412 | NORECIPIENT / NOTEXTTOSEND | malformed PRIVMSG |
| 421 | UNKNOWNCOMMAND | unknown command |
| 422 | NOMOTD | no MOTD file |
| 431/432/433 | NONICKNAMEGIVEN / ERRONEUSNICKNAME / NICKNAMEINUSE | `NICK` |
| 441/442/443 | USERNOTINCHANNEL / NOTONCHANNEL / USERONCHANNEL | membership errors |
| 451 | NOTREGISTERED | command before registration |
| 461 | NEEDMOREPARAMS | arity |
| 462 | ALREADYREGISTRED | re-registration |
| 464 | PASSWDMISMATCH | wrong/missing `PASS` |
| 471/473/475 | CHANNELISFULL / INVITEONLYCHAN / BADCHANNELKEY | `+l` / `+i` / `+k` |
| 472 | UNKNOWNMODE | unimplemented mode letter |
| 476 | BADCHANMASK | malformed channel name |
| 481/482 | NOPRIVILEGES / CHANOPRIVSNEEDED | privilege checks |
| 501/502 | UMODEUNKNOWNFLAG / USERSDONTMATCH | user-mode errors |
| 525 | INVALIDKEY | malformed `+k` key |
| 696 | INVALIDMODEPARAM | malformed mode parameter |

## Appendix B — limits

From `include/Limits.hpp`. Know these numbers by heart.

| Constant | Value |
| --- | --- |
| nickname | 9 |
| username | 10 (truncated) |
| channel name | 50 |
| topic | 390 |
| message line | 512 including CRLF |
| channel key | 23 |
| SendQ | 64 KiB |
| max clients | 1024 |
| user limit (`+l`) | 65535 |
| PING interval / timeout | 120 s / 120 s |

## Appendix C — one-page checklist

Print this. Tick it as you go.

```
PRE-FLIGHT
[ ] empty dir, git clone, git remote -v matches the student
[ ] alias | grep -E 'make|nc|valgrind'      -> empty
[ ] git submodule update --init --recursive

INSTANT-ZERO GATES
[ ] make re                                  -> no warnings
[ ] ls ircserv                               -> exists
[ ] grep -rE 'poll|epoll_wait|select' src/   -> EXACTLY 1
[ ] grep -rn fcntl src/                      -> only F_SETFL, O_NONBLOCK
[ ] grep -n EAGAIN src/                      -> 0 hits on I/O paths

NETWORKING
[ ] ss -ltnp | grep 6667                     -> 0.0.0.0:6667
[ ] nc -C connect, register, get 001
[ ] HexChat connects
[ ] 10 parallel clients + HexChat together
[ ] channel broadcast reaches everyone but the sender

SPECIALS
[ ] partial command, others still responsive
[ ] kill -9 a client, server survives, new client connects
[ ] kill -9 mid-command, no hang
[ ] ^Z + flood: CPU ~0%, no hang, resumes cleanly

COMMANDS
[ ] PASS / NICK / USER matrices
[ ] PRIVMSG matrix, user and channel
[ ] KICK INVITE TOPIC MODE, as regular AND as operator
[ ] all 32 MODE combinations
[ ] +i +t +k +l +o each verified end to end

ROBUSTNESS
[ ] grammar torture, no crash
[ ] valgrind: definitely lost = 0
[ ] leaks checked DURING the ^Z flood
[ ] fd count returns to baseline after 200 churn cycles

BONUS (only if all the above is perfect)
[ ] file transfer works in HexChat
[ ] ../etc/passwd refused
```

**See also:**
[RFC-CONFORMANCE.md](RFC-CONFORMANCE.md) · [NETWORKING.md](NETWORKING.md) ·
[TESTS.md](TESTS.md)
