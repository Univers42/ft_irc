# Cheatsheet to test and attack the project deeply to be sure everything is working fine...

##  norm test
##  verify if there are duplications of poll, select, epool_wait, kevent

```bash
# grep way

matches=0
total=0
for f in poll select epoll_wait kevent; do
    count=$(grep -RhoE "\b${f}\s*\(" src/ | wc -l)
    if (( count > 0 )); then
        ((matches++))
        total=$((total + count))
        if (( count == 1 )); then
            printf "%-12s %d\n" "$f" "$count"
        else
            printf "Error: %-12s %d occurences\n" "$f" "$count"
            grep -RnE "\b${f}\s*\(" src/
        fi
    fi
done

if ((matches != 1 || total != 1)); then
    echo "ERROR: exactly one of poll/select/epoll_wait/kevent must be called exactly once."
    exit 1
fi


```



##  README italic <section,ressources,etc..>

---

# Running the attack suite

Everything below is automated in `tests/`. See `tests/README.md` for the full
map; the short version:

```bash
make                     # build ./ircserv
cd tests
bash ./run_all.sh        # the whole suite against a live server
./run_dual.sh            # run it twice, under bash and hellish, and diff
./shell_conformance.sh   # differential shell prober (bash vs hellish)
```

`12_build_norm.sh` automates the norm checks that used to live in this file as
loose snippets — the poll/select/epoll_wait/kevent count, the `fcntl` form, the
forbidden calls, the compiler flags. It scans source with comments and string
literals stripped, because diagnostics like

```cpp
throw std::runtime_error("epoll_wait() failed: " + ...);
```

otherwise get counted as extra call sites.

Current result: **26/26**, one `epoll_wait()` call site, all four `fcntl()`
calls in the allowed `fcntl(fd, F_SETFL, O_NONBLOCK)` form.

# Attacking the shell, not just the server

Running the same suite under a second shell turns the tests into a differential
oracle: identical scripts, identical server, so any divergence is the shell.
Doing this against `hellish` 2.3.2 surfaced five defects, all filed with
reproducers:

| # | Defect | Severity |
|---|---|---|
| [11](https://github.com/Univers42/hellish/issues/11) | Command substitution is brace-expanded and **executed once per alternative** — `$(cmd '{a,b}')` runs `cmd` twice, duplicating side effects | high |
| [12](https://github.com/Univers42/hellish/issues/12) | `export A B C` assigns `A=B` and never exports `B` | high |
| [13](https://github.com/Univers42/hellish/issues/13) | `$!` is a wrapper pid, so `kill "$!"` orphans the real process and `kill -STOP "$!"` silently does nothing | high |
| [14](https://github.com/Univers42/hellish/issues/14) | `$0` empty for `-c` with no operand | low |
| [15](https://github.com/Univers42/hellish/issues/15) | `${var:?}` prints an empty diagnostic | low |

Two of these were only findable by running real work through the shell rather
than probing it in isolation — #13 showed up as three *server* assertions
failing in `04_disconnect.sh`, and only turned out to be the shell after the
same script passed under bash.

# Bugs this found in our own test harness

Worth recording, because each one was silently weakening the suite:

- **Three test files were byte-identical copies of other test files.**
  `priv_msg.sh` was a copy of `disconnect.sh`, `kick_invite_topic.sh` a copy of
  `channel_jjoint_part.sh`, `pre-registratoin.sh` a copy of `modes.sh`. Half the
  claimed coverage did not exist. All three are now written (68 assertions).
- **`irc_expect()` used a global `i`**, destroying the caller's loop counter.
  The 12-client stress loop reconnected client `s1` over and over.
- **`irc_server_alive()` ran `exec 3>&- 3<&- 2>/dev/null` in the current
  shell**, which permanently redirected the *script's* stderr to `/dev/null`.
  Every warning after the first liveness check vanished. It only showed up in
  the bash/hellish diff, because hellish has no `/dev/tcp` and so never reached
  that line.
- **A test registered an 11-character nick** (`probeclient`) against a server
  advertising `NICKLEN=9`, then messaged the untruncated name and got 401. It
  had been reported as "server stalled on the frozen client" — a server bug that
  was never there.
