# 07 — File transfer

**Context.** Alice wants to send Bob a file. There are two ways, and they are
genuinely different: **DCC**, which real clients speak and the server merely
forwards, and **`FILE`**, an original server-mediated protocol that works from
plain `nc`.

> **Requires `make bonus` or `make`.** On the `make mandatory` binary, `FILE`
> answers `421 :Unknown command`.

---

## HexChat — DCC

`/dcc send bob photo.jpg`, or **right-click a user → Send file**.

The server does not implement DCC. A DCC handshake is a CTCP payload — bytes
wrapped in `\x01` inside an ordinary `PRIVMSG` — and the server's line
sanitizer strips stray `\r` and NUL but leaves `\x01` untouched, so the payload
arrives byte-identical and the two clients negotiate their own direct
connection. Nothing special is required for this to work; it works because the
server does *not* mangle what it relays.

---

## netcat — the `FILE` protocol

DCC needs two clients that can open sockets to each other. `FILE` needs
nothing but the connection you already have — the server relays base64 chunks
between the two parties. It **never decodes them and never touches disk**.

A complete transfer, captured:

```
alice: FILE SEND bob hello.txt 12
```
```
alice sees: :ft_irc NOTICE alice :FILE 1 offered to bob
bob sees:   :alice!a@127.0.0.1 FILE OFFER 1 hello.txt 12
```

```
bob:   FILE ACCEPT 1
```
```
alice sees: :bob!b@127.0.0.1 FILE OK 1
```

```
alice: FILE DATA 1 aGVsbG8gd29ybGQh
alice: FILE END 1
```
```
bob sees: :alice!a@127.0.0.1 FILE DATA 1 aGVsbG8gd29ybGQh
bob sees: :alice!a@127.0.0.1 FILE END 1 12
```

Bob decodes locally: `echo aGVsbG8gd29ybGQh | base64 -d` → `hello world!`.

---

## Verbs

| Command | Meaning |
| --- | --- |
| `FILE SEND <nick> <file> <size>` | Offer a file. The server assigns the id |
| `FILE ACCEPT <id>` / `FILE REJECT <id>` | Receiver's answer — `FILE OK` / `FILE NO` back to the sender |
| `FILE DATA <id> <base64>` | One chunk |
| `FILE END <id>` | Finish — the receiver's copy carries the byte count |
| `FILE ABORT <id>` | Cancel from either side |

Sending `FILE` with no arguments prints the usage line:

```
:ft_irc NOTICE alice :FILE usage: SEND <nick> <file> <size> | ACCEPT/REJECT <id> | DATA <id> <b64> | END <id> | ABORT <id>
```

**Always use the id the server gave you** — it increments per offer, and it is
what pairs a chunk with a transfer.

**Errors come back as `NOTICE`, not numerics.** `FILE` is not an RFC command,
so it has no numeric of its own; inventing one would collide with a real
numeric someday.

---

## Flow control and timeouts

If the receiver falls behind, the sender gets:

```
:ft_irc NOTICE alice :FILE WAIT 1
```

That fires when the receiver's send queue passes half of its 64 KiB budget.
Pause, then continue — ignoring it eventually overflows the queue and
disconnects the *receiver*. A transfer idle for **60 seconds** is aborted from
both ends with `FILE ABRT <id> :<reason>`.

---

## Why relay-only

The server never decodes a chunk, never buffers a whole file, and never writes
one. It validates the framing and forwards the payload. That means a malicious
"file" is just base64 text passing through a bounded queue — there is no
decoder to overflow, no temp file to fill a disk, and no path to traverse. The
cost is that the receiver does the decoding, which is one `base64 -d`.

---

## Checks

* `FILE SEND` produces an id and an `OFFER` on the receiving side.
* `FILE ACCEPT` produces `FILE OK` on the sending side.
* Concatenating the received `FILE DATA` payloads and base64-decoding them
  reproduces the original file byte for byte.
* `FILE DATA` with a wrong or foreign id is refused.
* On `make mandatory`, `FILE` gives `421`.

Guarded by `tests/test_filetransfer.cpp`. Full protocol spec in
[`../DOCUMENTATION.md`](../DOCUMENTATION.md).

**Next:** [08 — Failure & resilience](08-resilience.md)
