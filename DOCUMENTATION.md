# ft_irc — Technical Documentation

## Table of Contents

1. [Project Overview](#project-overview)
2. [Architecture](#architecture)
3. [File Structure](#file-structure)
4. [Build System](#build-system)
5. [Core Components](#core-components)
   - [Server](#server)
   - [Client](#client)
   - [Channel](#channel)
   - [Message Parser](#message-parser)
6. [Command Reference](#command-reference)
   - [Registration Commands](#registration-commands)
   - [Channel Commands](#channel-commands)
   - [Messaging Commands](#messaging-commands)
   - [Operator Commands](#operator-commands)
   - [Query Commands](#query-commands)
7. [Channel Modes](#channel-modes)
8. [IRC Numeric Replies](#irc-numeric-replies)
9. [HexChat Compatibility](#hexchat-compatibility)
10. [Bonus Features](#bonus-features)
    - [Bot](#bot)
    - [File Transfer (DCC)](#file-transfer-dcc)
11. [Error Handling & Robustness](#error-handling--robustness)
12. [Testing](#testing)
13. [Subject Compliance Matrix](#subject-compliance-matrix)

---

## Project Overview

ft_irc is an IRC (Internet Relay Chat) server written in **C++98**. It implements the IRC protocol per **RFC 2812** and is designed to work seamlessly with **HexChat** as the reference client. The server uses **epoll()** for non-blocking I/O multiplexing on Linux, handling multiple simultaneous clients in a single-threaded event loop without forking.

### Key Design Decisions

- **I/O multiplexing**: `epoll()` chosen over `poll()`/`select()` for superior scalability (O(1) event notification vs O(n) scanning)
- **Single-threaded**: No threads or forks — one epoll loop handles all read/write/listen/accept operations
- **Non-blocking everything**: All file descriptors (listen socket + client sockets) set with `fcntl(fd, F_SETFL, O_NONBLOCK)`
- **Partial message reassembly**: TCP stream data is buffered per-client and split on `\r\n` boundaries
- **HexChat-first**: All numeric replies and protocol behaviors tuned for HexChat compatibility

---

## Architecture

```
                    ┌─────────────────────────────────────────┐
                    │              Server (epoll)             │
                    │                                         │
                    │  ┌───────────┐    ┌──────────────────┐  │
   Client ──TCP──→  │  │ Listen FD │    │   epoll_wait()   │  │
                    │  └─────┬─────┘    └────────┬─────────┘  │
                    │        │                   │             │
                    │        ▼                   ▼             │
                    │  ┌──────────┐    ┌──────────────────┐   │
                    │  │ accept() │    │ handleClientI/O  │   │
                    │  └────┬─────┘    └────────┬─────────┘   │
                    │       │                   │              │
                    │       ▼                   ▼              │
                    │  ┌──────────────────────────────────┐   │
                    │  │         Client Objects            │   │
                    │  │   (recv buffer, send buffer, fd)  │   │
                    │  └──────────────┬───────────────────┘   │
                    │                 │                        │
                    │                 ▼                        │
                    │  ┌──────────────────────────────────┐   │
                    │  │       Message::parse()            │   │
                    │  │   → dispatchCommand()             │   │
                    │  └──────────────┬───────────────────┘   │
                    │                 │                        │
                    │      ┌─────────┼─────────┐              │
                    │      ▼         ▼         ▼              │
                    │  ┌───────┐ ┌───────┐ ┌───────┐          │
                    │  │Channel│ │Channel│ │  Bot  │          │
                    │  │  #foo │ │  #bar │ │ircbot │          │
                    │  └───────┘ └───────┘ └───────┘          │
                    └─────────────────────────────────────────┘
```

### Data Flow

1. `epoll_wait()` returns readable/writable events
2. **EPOLLIN on listen fd** → `accept()` new client, set non-blocking, add to epoll
3. **EPOLLIN on client fd** → `recv()` → append to client's recv buffer → `extractMessages()` splits on `\r\n` → `handleMessage()` for each complete message
4. **EPOLLOUT on client fd** → `send()` from client's send buffer → handle partial sends
5. **EPOLLERR/EPOLLHUP** → disconnect client
6. Every 30 seconds: check for idle clients, send PING / disconnect timeouts

---

## File Structure

```
ft_irc/
├── Makefile                        # Build system (NAME, all, clean, fclean, re)
├── README.md                       # Project README per subject requirements
├── DOCUMENTATION.md                # This file
├── include/
│   ├── Replies.hpp                 # IRC numeric reply codes + server constants
│   ├── Client.hpp                  # Client class declaration
│   ├── Channel.hpp                 # Channel class declaration
│   ├── Message.hpp                 # Message parser declaration
│   ├── Server.hpp                  # Server class declaration
│   └── Bot.hpp                     # Bot class declaration (bonus)
├── src/
│   ├── main.cpp                    # Entry point, argument validation, signals
│   ├── Server.cpp                  # Server core: socket, epoll, event loop
│   ├── Client.cpp                  # Client: buffers, getters/setters
│   ├── Channel.cpp                 # Channel: members, modes, broadcast
│   ├── Message.cpp                 # IRC message parser
│   ├── CommandRegistration.cpp     # CAP, PASS, NICK, USER, registration
│   ├── CommandChannel.cpp          # JOIN, PART
│   ├── CommandMessaging.cpp        # PRIVMSG, NOTICE, PING, PONG, QUIT
│   ├── CommandOperator.cpp         # KICK, INVITE, TOPIC, MODE
│   ├── CommandQuery.cpp            # WHO, WHOIS, USERHOST
│   └── Bot.cpp                     # Bot commands (bonus)
└── ircd/                           # Reference: original IRC 2.0 C codebase (submodule)
```

---

## Build System

### Makefile

| Target   | Action                                    |
|----------|-------------------------------------------|
| `all`    | Build `ircserv` binary                    |
| `clean`  | Remove `obj/` directory                   |
| `fclean` | Remove `obj/` directory and `ircserv` binary |
| `re`     | `fclean` + `all` (full rebuild)           |

### Compiler Settings

```makefile
CXX      = c++
CXXFLAGS = -Wall -Wextra -Werror -std=c++98
```

Object files are placed in `obj/` — the directory is created automatically. Header search path is `include/`.

### Usage

```bash
make          # Build
./ircserv 6667 mypassword    # Run
make re       # Full rebuild
```

---

## Core Components

### Server

**File**: `include/Server.hpp` + `src/Server.cpp`

The central class managing the entire IRC server lifecycle.

#### Key Members

| Member | Type | Purpose |
|--------|------|---------|
| `_port` | `int` | TCP listen port |
| `_password` | `std::string` | Server-wide connection password |
| `_listenFd` | `int` | Listening socket file descriptor |
| `_epollFd` | `int` | epoll instance file descriptor |
| `_clients` | `std::map<int, Client*>` | fd → Client object mapping |
| `_channels` | `std::map<std::string, Channel*>` | name → Channel object mapping |
| `_bot` | `Bot*` | Internal bot instance (bonus) |
| `_lastPingCheck` | `time_t` | Timestamp of last idle check |
| `isRunning` | `static bool` | Signal-driven shutdown flag |

#### Socket Setup

1. `socket(AF_INET, SOCK_STREAM, 0)` — create TCP socket
2. `setsockopt(SO_REUSEADDR)` — allow port reuse
3. `fcntl(fd, F_SETFL, O_NONBLOCK)` — non-blocking mode
4. `bind()` to `INADDR_ANY:port`
5. `listen(fd, SOMAXCONN)` — start listening

#### Event Loop (`run()`)

```
while (isRunning):
    nfds = epoll_wait(epollFd, events, 64, 1000ms)
    for each event:
        if listen_fd → acceptClient()
        if EPOLLIN  → handleClientInput(fd)
        if EPOLLOUT → handleClientOutput(fd)
        if ERR/HUP  → disconnectClient(fd)
    checkTimeouts()
```

#### Client Lifecycle

1. **Accept**: `accept()` → `fcntl(O_NONBLOCK)` → `new Client(fd, hostname)` → add to epoll
2. **Register**: Client sends PASS/NICK/USER → `completeRegistration()` validates password and sends welcome burst (001–005 + 422)
3. **Active**: Dispatched commands, channel membership, messaging
4. **Disconnect**: Remove from all channels → broadcast QUIT → close fd → delete Client

#### Command Dispatch

`dispatchCommand()` routes by command string. Pre-registration commands (CAP, PASS, NICK, USER, QUIT, PONG) are always allowed. All others require `client->isRegistered()` or receive `ERR_NOTREGISTERED` (451).

---

### Client

**File**: `include/Client.hpp` + `src/Client.cpp`

Represents a single connected IRC client.

#### Key Members

| Member | Type | Purpose |
|--------|------|---------|
| `_fd` | `int` | Socket file descriptor |
| `_nickname` | `std::string` | IRC nickname (default: `"*"`) |
| `_username` | `std::string` | Username from USER command |
| `_realname` | `std::string` | Real name from USER command |
| `_hostname` | `std::string` | IP address from `inet_ntoa()` |
| `_password` | `std::string` | Password from PASS command |
| `_registered` | `bool` | Registration completed flag |
| `_recvBuffer` | `std::string` | Incoming TCP data buffer |
| `_sendBuffer` | `std::string` | Outgoing message queue |
| `_lastActivity` | `time_t` | Last message timestamp |
| `_pingSent` | `bool` | Awaiting PONG response |

#### Buffer Management

- **`appendToRecvBuffer(data)`**: Appends raw TCP data
- **`extractMessages()`**: Splits buffer on `\r\n` (or bare `\n`), returns complete messages. Handles:
  - Partial data (waits for terminator)
  - Multiple messages in one recv
  - Buffer overflow protection (force-flush at 512 bytes)
- **`queueMessage(msg)`**: Appends `msg + "\r\n"` to send buffer
- **`clearSendBuffer(n)`**: Erases first n bytes (after successful `send()`)

#### Prefix

`getPrefix()` returns `nick!user@host` format used in IRC message source.

---

### Channel

**File**: `include/Channel.hpp` + `src/Channel.cpp`

Represents an IRC channel with members, modes, and operators.

#### Key Members

| Member | Type | Purpose |
|--------|------|---------|
| `_name` | `std::string` | Channel name (e.g., `#general`) |
| `_topic` | `std::string` | Current topic |
| `_topicSetter` | `std::string` | Nickname who set the topic |
| `_topicTime` | `time_t` | When topic was set |
| `_creationTime` | `time_t` | When channel was created |
| `_key` | `std::string` | Channel password (mode +k) |
| `_userLimit` | `size_t` | Max members (mode +l, 0=unlimited) |
| `_inviteOnly` | `bool` | Invite-only flag (mode +i) |
| `_topicRestricted` | `bool` | Ops-only topic (mode +t) |
| `_members` | `std::map<int, Client*>` | fd → Client mapping |
| `_operators` | `std::set<int>` | Set of operator fds |
| `_inviteList` | `std::set<std::string>` | Invited nicknames |

#### Channel Creation

When a client JOINs a non-existent channel, `createChannel()` creates it with the client as both member and operator.

#### Broadcasting

`broadcastMessage(msg, exclude)` sends to all members except the optional `exclude` client (used for PRIVMSG to channel — sender doesn't receive their own message).

---

### Message Parser

**File**: `include/Message.hpp` + `src/Message.cpp`

Parses raw IRC protocol messages per RFC 2812 format:

```
[:prefix] COMMAND [param1] [param2] ... [:trailing]
```

#### `Message::parse(raw)`

1. Skip leading whitespace
2. Extract optional prefix (starts with `:`)
3. Extract command (uppercase it)
4. Extract parameters:
   - Regular params separated by spaces
   - Trailing param after `:` captures everything remaining

#### Example

Input: `:nick!user@host PRIVMSG #channel :Hello world!`

Result:
- `prefix`: `"nick!user@host"`
- `command`: `"PRIVMSG"`
- `params[0]`: `"#channel"`
- `params[1]`: `"Hello world!"`

---

## Command Reference

### Registration Commands

**File**: `src/CommandRegistration.cpp`

| Command | Syntax | Description |
|---------|--------|-------------|
| `CAP` | `CAP LS` / `CAP END` | Capability negotiation (empty list — no caps supported) |
| `PASS` | `PASS <password>` | Set connection password (must match server password) |
| `NICK` | `NICK <nickname>` | Set or change nickname. Validated: first char alpha/special, rest alnum/special/dash, max 9 chars |
| `USER` | `USER <user> <mode> <unused> :<realname>` | Set username and real name |

#### Registration Flow

1. Client sends PASS, NICK, USER (in any order, but PASS must come before registration completes)
2. When both NICK and USER are set, `completeRegistration()` fires:
   - Checks password → `ERR_PASSWDMISMATCH` (464) + disconnect on failure
   - Sends welcome burst: `RPL_WELCOME` (001), `RPL_YOURHOST` (002), `RPL_CREATED` (003), `RPL_MYINFO` (004), `RPL_ISUPPORT` (005), `ERR_NOMOTD` (422)

### Channel Commands

**File**: `src/CommandChannel.cpp`

| Command | Syntax | Description |
|---------|--------|-------------|
| `JOIN` | `JOIN <channel>[,<channel>] [<key>[,<key>]]` | Join one or more channels. Creates channel if new (creator = operator). Checks +i, +k, +l restrictions |
| `PART` | `PART <channel>[,<channel>] [:<reason>]` | Leave one or more channels. Removes empty channels |

#### JOIN Response Sequence

1. Broadcast `JOIN` to all channel members
2. Send topic: `RPL_TOPIC` (332) + `RPL_TOPICWHOTIME` (333), or `RPL_NOTOPIC` (331)
3. Send names: `RPL_NAMREPLY` (353) + `RPL_ENDOFNAMES` (366)

### Messaging Commands

**File**: `src/CommandMessaging.cpp`

| Command | Syntax | Description |
|---------|--------|-------------|
| `PRIVMSG` | `PRIVMSG <target>[,<target>] :<text>` | Send message to user or channel. Supports comma-separated targets |
| `NOTICE` | `NOTICE <target>[,<target>] :<text>` | Like PRIVMSG but never generates error replies |
| `PING` | `PING <token>` | Client ping → server replies `PONG` |
| `PONG` | `PONG <token>` | Client response to server PING — resets idle timer |
| `QUIT` | `QUIT [:<reason>]` | Disconnect with optional reason |

### Operator Commands

**File**: `src/CommandOperator.cpp`

| Command | Syntax | Description |
|---------|--------|-------------|
| `KICK` | `KICK <channel> <user> [:<reason>]` | Eject user from channel (operator only) |
| `INVITE` | `INVITE <nick> <channel>` | Invite user to channel (operator required if +i) |
| `TOPIC` | `TOPIC <channel> [:<topic>]` | Query or set topic (operator required if +t) |
| `MODE` | `MODE <target> [<modes> [<params>]]` | Query or change modes |

### Query Commands

**File**: `src/CommandQuery.cpp`

| Command | Syntax | Description |
|---------|--------|-------------|
| `WHO` | `WHO <channel\|nick>` | List channel members or find user. Returns `RPL_WHOREPLY` (352) + `RPL_ENDOFWHO` (315) |
| `WHOIS` | `WHOIS <nick>` | User info. Returns 311, 312, 319, 318 |
| `USERHOST` | `USERHOST <nick> [<nick>...]` | Quick user lookup (up to 5). Returns `RPL_USERHOST` (302) |

---

## Channel Modes

| Mode | Param | Description |
|------|-------|-------------|
| `+i` | — | Invite-only: only invited users can JOIN |
| `-i` | — | Remove invite-only restriction |
| `+t` | — | Topic restricted: only operators can change TOPIC |
| `-t` | — | Allow any member to change TOPIC |
| `+k` | `<key>` | Set channel password — must provide correct key to JOIN |
| `-k` | `[*]` | Remove channel password |
| `+o` | `<nick>` | Grant operator status to a member |
| `-o` | `<nick>` | Revoke operator status from a member |
| `+l` | `<limit>` | Set maximum user count |
| `-l` | — | Remove user limit |

Mode changes are broadcast to all channel members as: `:nick!user@host MODE #channel +/-modes [params]`

Unknown mode characters return `ERR_UNKNOWNMODE` (472).

---

## IRC Numeric Replies

### Registration & Info

| Code | Name | When Sent |
|------|------|-----------|
| 001 | `RPL_WELCOME` | After successful registration |
| 002 | `RPL_YOURHOST` | After successful registration |
| 003 | `RPL_CREATED` | After successful registration |
| 004 | `RPL_MYINFO` | After successful registration |
| 005 | `RPL_ISUPPORT` | After successful registration (CHANTYPES, PREFIX, CHANMODES, etc.) |
| 221 | `RPL_UMODEIS` | Response to MODE <nick> query |
| 302 | `RPL_USERHOST` | Response to USERHOST |
| 311 | `RPL_WHOISUSER` | WHOIS user info |
| 312 | `RPL_WHOISSERVER` | WHOIS server info |
| 319 | `RPL_WHOISCHANNELS` | WHOIS channel list |
| 318 | `RPL_ENDOFWHOIS` | End of WHOIS |
| 324 | `RPL_CHANNELMODEIS` | Response to MODE #channel query |
| 329 | `RPL_CREATIONTIME` | Channel creation timestamp |
| 331 | `RPL_NOTOPIC` | No topic set |
| 332 | `RPL_TOPIC` | Current topic |
| 333 | `RPL_TOPICWHOTIME` | Topic setter and time |
| 341 | `RPL_INVITING` | INVITE confirmation |
| 352 | `RPL_WHOREPLY` | WHO response entry |
| 315 | `RPL_ENDOFWHO` | End of WHO |
| 353 | `RPL_NAMREPLY` | NAMES channel member list |
| 366 | `RPL_ENDOFNAMES` | End of NAMES |
| 422 | `ERR_NOMOTD` | No MOTD file (sent after registration) |

### Error Replies

| Code | Name | Trigger |
|------|------|---------|
| 401 | `ERR_NOSUCHNICK` | Target nickname not found |
| 403 | `ERR_NOSUCHCHANNEL` | Channel does not exist |
| 404 | `ERR_CANNOTSENDTOCHAN` | Cannot send to channel (not a member) |
| 411 | `ERR_NORECIPIENT` | PRIVMSG with no target |
| 412 | `ERR_NOTEXTTOSEND` | PRIVMSG with no text |
| 421 | `ERR_UNKNOWNCOMMAND` | Unrecognized command |
| 431 | `ERR_NONICKNAMEGIVEN` | NICK with no parameter |
| 432 | `ERR_ERRONEUSNICKNAME` | Invalid nickname format |
| 433 | `ERR_NICKNAMEINUSE` | Nickname already taken |
| 441 | `ERR_USERNOTINCHANNEL` | Target not in channel |
| 442 | `ERR_NOTONCHANNEL` | You're not in that channel |
| 443 | `ERR_USERONCHANNEL` | User already in channel (INVITE) |
| 451 | `ERR_NOTREGISTERED` | Command requires registration |
| 461 | `ERR_NEEDMOREPARAMS` | Missing parameters |
| 462 | `ERR_ALREADYREGISTRED` | Already registered |
| 464 | `ERR_PASSWDMISMATCH` | Wrong password → disconnect |
| 471 | `ERR_CHANNELISFULL` | Channel at user limit (+l) |
| 472 | `ERR_UNKNOWNMODE` | Unknown mode character |
| 473 | `ERR_INVITEONLYCHAN` | Channel is invite-only (+i) |
| 475 | `ERR_BADCHANNELKEY` | Wrong channel key (+k) |
| 476 | `ERR_BADCHANMASK` | Invalid channel name |
| 482 | `ERR_CHANOPRIVSNEEDED` | Not a channel operator |
| 502 | `ERR_USERSDONTMATCH` | Can't change other user's mode |

---

## HexChat Compatibility

HexChat is the chosen reference client. The following behaviors ensure full compatibility:

### Connection Sequence

HexChat sends (in order): `CAP LS 302` → `PASS` → `NICK` → `USER` → `CAP END`

Our server:
1. Replies to `CAP LS` with empty capability list (`:ft_irc CAP * LS :`)
2. Stores password from `PASS`
3. Sets nickname from `NICK`
4. Completes registration on `USER` (both NICK and USER received)
5. Ignores `CAP END` (no-op)

### Required Numerics

HexChat expects the full 001–005 welcome burst. Without `RPL_ISUPPORT` (005), HexChat may not properly display channel prefixes or modes.

### Post-Registration Behavior

HexChat automatically sends:
- `MODE <nick> +i` after connecting → silently accepted (no user modes supported)
- `WHO #channel` after joining → returns member list with `@` flags
- `MODE #channel` on join → returns current modes + creation time
- `USERHOST <nick>` periodically → returns user info
- `WHOIS <nick>` on user interaction → returns full user info

### RPL_ISUPPORT Tokens

```
CHANTYPES=#  PREFIX=(o)@  CHANMODES=,k,,itl  NICKLEN=9
CHANNELLEN=50  TOPICLEN=390  NETWORK=ft_irc  CASEMAPPING=ascii
```

---

## Bonus Features

### Bot

**File**: `include/Bot.hpp` + `src/Bot.cpp`

An internal IRC bot named `ircbot` that responds to private messages.

#### Architecture

The bot is **not** a real connected client — it has no file descriptor. It's an internal Server object. When a `PRIVMSG ircbot :...` is received, `Server::cmdPrivmsg()` routes to `Bot::handleMessage()` instead of looking up a Client.

The bot's nickname (`ircbot`) is reserved — no client can use it (`ERR_NICKNAMEINUSE`).

#### Commands

| Command | Description | Example Response |
|---------|-------------|------------------|
| `!help` | List available commands | Shows all commands with descriptions |
| `!time` | Current server time | `Server time: 2025-06-15 14:23:01` |
| `!info` | Server information | `Server: ft_irc v1.0` |
| `!info #channel` | Channel information | `Channel #test: 3 users, modes: +nt` |
| `!joke` | Random programming joke | One of 8 hardcoded jokes |

#### Usage

```
/msg ircbot !help
/msg ircbot !time
/msg ircbot !info #general
/msg ircbot !joke
```

### File Transfer (DCC)

DCC (Direct Client-to-Client) file transfer is supported through natural PRIVMSG relay.

#### How It Works

1. Sender initiates DCC in HexChat (right-click user → Send File)
2. HexChat sends: `PRIVMSG target :\x01DCC SEND filename ip port size\x01`
3. Our server relays this PRIVMSG to the target (CTCP `\x01` characters pass through transparently)
4. Target's HexChat receives the DCC request and shows a file transfer dialog
5. The actual file transfer occurs **peer-to-peer** directly between clients — the server only relays the handshake

No special DCC code is needed in the server — proper PRIVMSG relay of all message content (including CTCP control characters) is sufficient. The line sanitizer strips only `\0`/`\r`; `\x01` CTCP framing survives byte-for-byte (pinned by `FileTransferTest.DccSendHandshakeRelaysByteForByte`).

#### HexChat demo

1. Run two HexChat instances on the server (`alice`, `bob`).
2. In alice's window: right-click `bob` → *Send File* → pick a file.
3. The DCC handshake travels through `ircserv` as a relayed CTCP PRIVMSG; the payload then flows **peer-to-peer**. Watch the raw log (`Ctrl+Shift+L`) to see the relayed `\x01DCC SEND ...\x01` line.

### File Transfer (server-mediated `FILE` protocol)

The original bonus addition: a relay protocol for clients that cannot open
peer-to-peer connections (NAT, web gateways). The server validates and
relays base64 chunks — it never decodes them and never touches the disk.
Implemented as `FileTransferExt` (`src/bonus/FileTransferExt.cpp`) through
the extension seam's `onCommand` hook, so it can never shadow an RFC command.

```
sender:    FILE SEND <nick> <filename> <size>
  → recipient:  :<sender>  FILE OFFER <id> <filename> <size>
recipient: FILE ACCEPT <id>   |   FILE REJECT <id>
  → sender:     :<recipient> FILE OK <id>  |  FILE NO <id>
sender:    FILE DATA <id> <base64 ≤ 400 chars>     (relayed verbatim)
sender:    FILE END <id>
either:    FILE ABORT <id>
```

Guarantees: casemapped target lookup, filename sanitized (no `/`, `\`,
spaces, control chars), size capped at 50 MB via `strtoul` bounds, base64
charset + running size-overrun validation, one active transfer per
(sender, recipient), **flow control** (`FILE WAIT <id>` when the recipient's
send queue is half full — sender retries), 60 s idle abort, abort + peer
notification on disconnect.

#### Scripted demo (nc)

```bash
# terminal 1 — recipient
nc -C 127.0.0.1 6667
PASS pass / NICK bob / USER bob 0 * :Bob        # one per line
# wait for ":alice!… FILE OFFER 1 photo.jpg 1234", then:
FILE ACCEPT 1
# collect the FILE DATA chunks, then locally: base64 -d > photo.jpg

# terminal 2 — sender
nc -C 127.0.0.1 6667
PASS pass / NICK alice / USER alice 0 * :Alice
FILE SEND bob photo.jpg 1234
FILE DATA 1 $(base64 -w 400 photo.jpg | head -1)   # one line per chunk
FILE END 1
```

Byte-identical reassembly is pinned by
`FileTransferTest.HappyPathReassemblesByteIdentical`.

---

## Error Handling & Robustness

The subject requires: *"Your program should not crash in any circumstances (even when it runs out of memory), and should not quit unexpectedly."*

### Signal Handling

- **SIGPIPE** → `SIG_IGN` — Prevents crash when `send()` targets a closed connection
- **SIGINT** (Ctrl+C) → Sets `Server::isRunning = false` for graceful shutdown
- **SIGTERM** → Same graceful shutdown

### Out-of-Memory (OOM) Protection

All `new` allocations are wrapped in `try/catch (std::bad_alloc)`:

- `new Client()` in `acceptClient()` — on failure, closes the fd and returns
- `new Channel()` in `createChannel()` — on failure, returns NULL; `cmdJoin` handles gracefully
- `new Bot()` in constructor — on failure, bot is set to NULL; all bot interactions check `_bot != NULL`

### I/O Error Handling

- `recv()` returning 0 → connection closed → `disconnectClient()`
- `recv()` returning -1 with `EAGAIN`/`EWOULDBLOCK` → normal for non-blocking, ignored
- `send()` returning -1 with `EAGAIN`/`EWOULDBLOCK` → retry on next EPOLLOUT
- `accept()` failing → logged and ignored (DoS-resilient)
- `epoll_wait()` returning `EINTR` → continue (signal interrupted)

### Client Disconnect Safety

After processing each message, the code checks `_clients.find(fd) != _clients.end()` before continuing, because a command handler (e.g., wrong password → `disconnectClient()`) may have removed the client mid-iteration.

### Timeout Management

- Every 30 seconds, idle clients are checked
- After `PING_INTERVAL` (120s) of inactivity → server sends `PING`
- After `PING_INTERVAL + PING_TIMEOUT` (240s) with no response → disconnect

---

## Testing

### Compilation Test

```bash
make re  # Must compile with zero warnings under -Wall -Wextra -Werror -std=c++98
```

### Manual Testing with HexChat

1. Start server: `./ircserv 6667 test123`
2. Open HexChat → New Network → `127.0.0.1/6667` → Server Password: `test123`
3. Verify: welcome message, join channels, send messages, use MODE/KICK/INVITE/TOPIC

### Partial Data Test (nc)

```bash
# The nc -C test from the subject:
nc -C 127.0.0.1 6667
# Type "com" then Ctrl+D, "man" then Ctrl+D, "d" then Enter
# Server should reassemble "command" and process it
```

### Python Protocol Test

```python
import socket, time

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.connect(('127.0.0.1', 6667))
s.settimeout(2)

# Registration
s.send(b"PASS test123\r\n")
s.send(b"NICK tester\r\n")
s.send(b"USER tester 0 * :Test User\r\n")
time.sleep(0.5)
print(s.recv(4096).decode())  # Should show 001-005 welcome burst

# Channel operations
s.send(b"JOIN #test\r\n")
time.sleep(0.5)
print(s.recv(4096).decode())  # JOIN + NAMES

s.send(b"TOPIC #test :Hello World\r\n")
s.send(b"MODE #test +itk secret\r\n")
s.send(b"MODE #test\r\n")

s.send(b"QUIT :bye\r\n")
s.close()
```

### Stress Test: Partial Message Reassembly

```python
import socket, time

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.connect(('127.0.0.1', 6667))
s.settimeout(2)

# Send registration in fragments
s.send(b"PASS test")
time.sleep(0.1)
s.send(b"123\r\n")
s.send(b"NI")
time.sleep(0.1)
s.send(b"CK partial\r\n")
s.send(b"USER partial 0 * :Test\r\n")
time.sleep(0.5)
print(s.recv(4096).decode())  # Should register with nick "partial"

s.send(b"QUIT\r\n")
s.close()
```

---

## Subject Compliance Matrix

| # | Requirement | Status | Implementation |
|---|-------------|--------|----------------|
| 1 | Program name: `ircserv` | ✅ | Makefile `NAME = ircserv` |
| 2 | Makefile: NAME, all, clean, fclean, re | ✅ | All targets present |
| 3 | Compile with `c++` + `-Wall -Wextra -Werror` | ✅ | `CXX = c++`, `CXXFLAGS` set |
| 4 | C++98 standard | ✅ | `-std=c++98` flag |
| 5 | C++ headers over C headers | ✅ | `<cstring>`, `<cerrno>`, `<cstdlib>`, etc. |
| 6 | No external/Boost libraries | ✅ | Only stdlib + POSIX |
| 7 | No crash (even OOM) | ✅ | `try/catch(bad_alloc)` on all `new` |
| 8 | No unexpected quit | ✅ | Signal handlers, SIGPIPE ignored |
| 9 | `./ircserv <port> <password>` | ✅ | `main.cpp` validates |
| 10 | Handle multiple clients | ✅ | epoll event loop |
| 11 | No forking | ✅ | No `fork()` used |
| 12 | Non-blocking I/O | ✅ | `fcntl(fd, F_SETFL, O_NONBLOCK)` |
| 13 | Single poll/equivalent | ✅ | One `epoll` instance |
| 14 | Don't read/write without poll | ✅ | All I/O via epoll events |
| 15 | Reference client chosen | ✅ | HexChat |
| 16 | TCP/IP v4 | ✅ | `AF_INET` |
| 17 | Authenticate | ✅ | PASS → password check |
| 18 | Set nickname | ✅ | NICK command |
| 19 | Set username | ✅ | USER command |
| 20 | Join channel | ✅ | JOIN command |
| 21 | Private messages | ✅ | PRIVMSG command |
| 22 | Channel broadcast | ✅ | `broadcastMessage()` |
| 23 | Operators + regular users | ✅ | `_operators` set in Channel |
| 24 | KICK | ✅ | `cmdKick()` |
| 25 | INVITE | ✅ | `cmdInvite()` |
| 26 | TOPIC | ✅ | `cmdTopic()` |
| 27 | MODE i (invite-only) | ✅ | `setInviteOnly()` |
| 28 | MODE t (topic restricted) | ✅ | `setTopicRestricted()` |
| 29 | MODE k (channel key) | ✅ | `setKey()` / `removeKey()` |
| 30 | MODE o (operator) | ✅ | `setOperator()` |
| 31 | MODE l (user limit) | ✅ | `setUserLimit()` / `removeUserLimit()` |
| 32 | Partial data handling | ✅ | `extractMessages()` buffer |
| 33 | `fcntl()` only with `F_SETFL, O_NONBLOCK` | ✅ | Only usage |
| 34 | README: italicized first line | ✅ | `*This project has been created...by dlesieur.*` |
| 35 | README: Description section | ✅ | Present |
| 36 | README: Instructions section | ✅ | Compilation, running, HexChat, netcat |
| 37 | README: Resources + AI usage | ✅ | RFC links + AI disclosure |
| 38 | README in English | ✅ | English |
| 39 | Bonus: Bot | ✅ | `ircbot` with !help/!time/!info/!joke |
| 40 | Bonus: File transfer | ✅ | DCC via natural PRIVMSG relay |
