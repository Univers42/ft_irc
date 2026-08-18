#!/usr/bin/env python3
"""
A deliberately minimal, deliberately non-compliant stand-in IRC server.

This exists for ONE reason: so you can run `./run_all.sh` against something
and confirm the *test harness itself* works (correct regexes, correct
timing, no bugs in the scripts) before you point BIN at your real ft_irc
and start chasing phantom test failures that are actually harness bugs.

It is NOT a model answer for the project. It's Python, it's not C++98, it
doesn't implement half the RFC, and it will happily do things the real
subject forbids. Don't submit this, don't imitate its structure, don't
grade your own ircserv against its behavior beyond "does the harness pass."
"""
import re
import selectors
import socket
import sys
import time

sel = selectors.DefaultSelector()
clients = {}       # fd -> ClientConn
nick_to_fd = {}     # nick -> fd
channels = {}       # name -> Channel
SERVER_PASSWORD = None


class ClientConn:
    def __init__(self, sock, addr):
        self.sock = sock
        self.addr = addr
        self.rbuf = b""
        self.nick = None
        self.user = None
        self.pass_ok = False
        self.registered = False

    def fd(self):
        return self.sock.fileno()

    def prefix(self):
        return f"{self.nick}!{self.user or 'u'}@mock"


class Channel:
    def __init__(self, name):
        self.name = name
        self.members = set()
        self.operators = set()
        self.topic = None
        self.key = None
        self.limit = None
        self.invite_only = False
        self.topic_restricted = False
        self.invited = set()


def numeric(c, code, text):
    target = c.nick or "*"
    return f":mockserv {code} {target} {text}\r\n"


def sendline(c, raw):
    try:
        c.sock.sendall(raw.encode())
    except OSError:
        pass


def broadcast(chan, raw, exclude_fd=None):
    for fd in list(chan.members):
        if fd == exclude_fd:
            continue
        target = clients.get(fd)
        if target:
            sendline(target, raw)


def parse_line(line):
    line = line.rstrip("\r")
    if not line:
        return None, [], None
    trailing = None
    if " :" in line:
        head, trailing = line.split(" :", 1)
    else:
        head = line
    parts = head.split()
    if not parts:
        return None, [], trailing
    cmd = parts[0].upper()
    params = parts[1:]
    if trailing is not None:
        params.append(trailing)
    return cmd, params, trailing


def try_welcome(c):
    if c.pass_ok and c.nick and c.user and not c.registered:
        c.registered = True
        sendline(c, numeric(c, "001", f":Welcome to the mock IRC server {c.nick}"))


def handle_command(c, cmd, params, trailing):
    if cmd == "PASS":
        if c.registered:
            sendline(c, numeric(c, "462", ":You may not reregister"))
            return
        given = params[0] if params else ""
        c.pass_ok = given == SERVER_PASSWORD
        try_welcome(c)
        return

    if not c.registered and cmd not in ("PASS", "NICK", "USER", "PING", "QUIT"):
        sendline(c, numeric(c, "451", ":You have not registered"))
        return

    if cmd == "NICK":
        if not params:
            sendline(c, numeric(c, "431", ":No nickname given"))
            return
        newnick = params[0]
        if newnick in nick_to_fd and nick_to_fd[newnick] != c.fd():
            sendline(c, numeric(c, "433", f"{newnick} :Nickname is already in use"))
            return
        if c.nick and c.nick in nick_to_fd:
            del nick_to_fd[c.nick]
        c.nick = newnick
        nick_to_fd[newnick] = c.fd()
        try_welcome(c)
        return

    if cmd == "USER":
        if len(params) < 4:
            sendline(c, numeric(c, "461", "USER :Not enough parameters"))
            return
        c.user = params[0]
        try_welcome(c)
        return

    if cmd == "PING":
        token = params[0] if params else "mock"
        sendline(c, f":mockserv PONG mockserv :{token}\r\n")
        return

    if cmd == "QUIT":
        raise ConnectionResetError()

    if cmd == "JOIN":
        if not params:
            sendline(c, numeric(c, "461", "JOIN :Not enough parameters"))
            return
        chan_names = params[0].split(",")
        keys = params[1].split(",") if len(params) > 1 else []
        for i, name in enumerate(chan_names):
            key = keys[i] if i < len(keys) else None
            chan = channels.setdefault(name, Channel(name))
            is_new = len(chan.members) == 0
            if chan.key and key != chan.key:
                sendline(c, numeric(c, "475", f"{name} :Cannot join channel (+k)"))
                continue
            if chan.invite_only and c.fd() not in chan.invited and c.fd() not in chan.operators:
                sendline(c, numeric(c, "473", f"{name} :Cannot join channel (+i)"))
                continue
            if chan.limit is not None and len(chan.members) >= chan.limit:
                sendline(c, numeric(c, "471", f"{name} :Cannot join channel (+l)"))
                continue
            chan.members.add(c.fd())
            if is_new:
                chan.operators.add(c.fd())
            chan.invited.discard(c.fd())
            broadcast(chan, f":{c.prefix()} JOIN :{name}\r\n")
            if chan.topic:
                sendline(c, numeric(c, "332", f"{name} :{chan.topic}"))
        return

    if cmd == "PART":
        if not params:
            sendline(c, numeric(c, "461", "PART :Not enough parameters"))
            return
        name = params[0]
        chan = channels.get(name)
        if not chan or c.fd() not in chan.members:
            sendline(c, numeric(c, "442", f"{name} :You're not on that channel"))
            return
        broadcast(chan, f":{c.prefix()} PART {name}\r\n")
        chan.members.discard(c.fd())
        chan.operators.discard(c.fd())
        return

    if cmd == "PRIVMSG":
        if not params:
            sendline(c, numeric(c, "411", ":No recipient given"))
            return
        target = params[0]
        if len(params) < 2:
            sendline(c, numeric(c, "412", ":No text to send"))
            return
        text = params[-1]
        if target.startswith("#"):
            chan = channels.get(target)
            if not chan or c.fd() not in chan.members:
                sendline(c, numeric(c, "404", f"{target} :Cannot send to channel"))
                return
            broadcast(chan, f":{c.prefix()} PRIVMSG {target} :{text}\r\n", exclude_fd=c.fd())
        else:
            tfd = nick_to_fd.get(target)
            if not tfd:
                sendline(c, numeric(c, "401", f"{target} :No such nick/channel"))
                return
            sendline(clients[tfd], f":{c.prefix()} PRIVMSG {target} :{text}\r\n")
        return

    if cmd == "KICK":
        if len(params) < 2:
            sendline(c, numeric(c, "461", "KICK :Not enough parameters"))
            return
        name, target = params[0], params[1]
        reason = params[-1] if len(params) > 2 else target
        chan = channels.get(name)
        if not chan or c.fd() not in chan.operators:
            sendline(c, numeric(c, "482", f"{name} :You're not channel operator"))
            return
        tfd = nick_to_fd.get(target)
        if not tfd or tfd not in chan.members:
            sendline(c, numeric(c, "441", f"{target} {name} :They aren't on that channel"))
            return
        broadcast(chan, f":{c.prefix()} KICK {name} {target} :{reason}\r\n")
        chan.members.discard(tfd)
        chan.operators.discard(tfd)
        return

    if cmd == "INVITE":
        if len(params) < 2:
            sendline(c, numeric(c, "461", "INVITE :Not enough parameters"))
            return
        target, name = params[0], params[1]
        chan = channels.get(name)
        if not chan or c.fd() not in chan.operators:
            sendline(c, numeric(c, "482", f"{name} :You're not channel operator"))
            return
        tfd = nick_to_fd.get(target)
        if not tfd:
            sendline(c, numeric(c, "401", f"{target} :No such nick/channel"))
            return
        chan.invited.add(tfd)
        sendline(c, numeric(c, "341", f"{target} {name}"))
        sendline(clients[tfd], f":{c.prefix()} INVITE {target} :{name}\r\n")
        return

    if cmd == "TOPIC":
        if not params:
            sendline(c, numeric(c, "461", "TOPIC :Not enough parameters"))
            return
        name = params[0]
        chan = channels.get(name)
        if not chan or c.fd() not in chan.members:
            sendline(c, numeric(c, "442", f"{name} :You're not on that channel"))
            return
        if len(params) == 1:
            if chan.topic:
                sendline(c, numeric(c, "332", f"{name} :{chan.topic}"))
            else:
                sendline(c, numeric(c, "331", f"{name} :No topic is set"))
            return
        if chan.topic_restricted and c.fd() not in chan.operators:
            sendline(c, numeric(c, "482", f"{name} :You're not channel operator"))
            return
        chan.topic = params[-1]
        broadcast(chan, f":{c.prefix()} TOPIC {name} :{chan.topic}\r\n")
        return

    if cmd == "MODE":
        if not params:
            sendline(c, numeric(c, "461", "MODE :Not enough parameters"))
            return
        name = params[0]
        chan = channels.get(name)
        if not chan:
            sendline(c, numeric(c, "403", f"{name} :No such channel"))
            return
        if len(params) == 1:
            return  # mode query, not exercised by the suite
        if c.fd() not in chan.operators:
            sendline(c, numeric(c, "482", f"{name} :You're not channel operator"))
            return
        flagstr = params[1]
        rest = params[2:]
        ai = 0
        sign = "+"
        applied = []
        for ch in flagstr:
            if ch in "+-":
                sign = ch
                continue
            if ch == "i":
                chan.invite_only = sign == "+"
                applied.append(sign + ch)
            elif ch == "t":
                chan.topic_restricted = sign == "+"
                applied.append(sign + ch)
            elif ch == "k":
                if sign == "+":
                    if ai >= len(rest):
                        sendline(c, numeric(c, "461", "MODE :Not enough parameters"))
                        return
                    chan.key = rest[ai]; ai += 1
                else:
                    chan.key = None
                applied.append(sign + ch)
            elif ch == "l":
                if sign == "+":
                    if ai >= len(rest):
                        sendline(c, numeric(c, "461", "MODE :Not enough parameters"))
                        return
                    try:
                        chan.limit = int(rest[ai])
                    except ValueError:
                        chan.limit = None
                    ai += 1
                else:
                    chan.limit = None
                applied.append(sign + ch)
            elif ch == "o":
                if ai >= len(rest):
                    sendline(c, numeric(c, "461", "MODE :Not enough parameters"))
                    return
                target = rest[ai]; ai += 1
                tfd = nick_to_fd.get(target)
                if tfd and tfd in chan.members:
                    if sign == "+":
                        chan.operators.add(tfd)
                    else:
                        chan.operators.discard(tfd)
                applied.append(sign + ch + " " + target)
            else:
                sendline(c, numeric(c, "472", f"{ch} :is unknown mode char to me"))
        if applied:
            broadcast(chan, f":{c.prefix()} MODE {name} " + " ".join(applied) + "\r\n")
        return

    sendline(c, numeric(c, "421", f"{cmd} :Unknown command"))


def drop_client(c):
    fd = c.fd()
    for chan in channels.values():
        if fd in chan.members:
            broadcast(chan, f":{c.prefix()} QUIT :Connection closed\r\n", exclude_fd=fd)
        chan.members.discard(fd)
        chan.operators.discard(fd)
        chan.invited.discard(fd)
    if c.nick and nick_to_fd.get(c.nick) == fd:
        del nick_to_fd[c.nick]
    try:
        sel.unregister(c.sock)
    except (KeyError, ValueError):
        pass
    try:
        c.sock.close()
    except OSError:
        pass
    clients.pop(fd, None)


def on_readable(sock):
    c = clients[sock.fileno()]
    try:
        chunk = c.sock.recv(4096)
    except (BlockingIOError, InterruptedError):
        return
    except OSError:
        drop_client(c)
        return
    if not chunk:
        drop_client(c)
        return
    c.rbuf += chunk
    while b"\n" in c.rbuf:
        line, c.rbuf = c.rbuf.split(b"\n", 1)
        text = line.decode(errors="replace")
        cmd, params, trailing = parse_line(text)
        if cmd is None:
            continue
        try:
            handle_command(c, cmd, params, trailing)
        except ConnectionResetError:
            drop_client(c)
            return
        except Exception as e:  # keep the mock alive no matter what
            sys.stderr.write(f"mock ircserv: swallowed exception: {e!r}\n")


def on_acceptable(sock):
    conn, addr = sock.accept()
    conn.setblocking(False)
    c = ClientConn(conn, addr)
    clients[conn.fileno()] = c
    sel.register(conn, selectors.EVENT_READ, data="client")


def main():
    global SERVER_PASSWORD
    if len(sys.argv) != 3:
        sys.stderr.write("usage: mock_ircserv.py <port> <password>\n")
        sys.exit(1)
    try:
        port = int(sys.argv[1])
    except ValueError:
        sys.stderr.write("port must be numeric\n")
        sys.exit(1)
    if not (1 <= port <= 65535):
        sys.stderr.write("port out of range\n")
        sys.exit(1)
    SERVER_PASSWORD = sys.argv[2]

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        srv.bind(("0.0.0.0", port))
    except OSError as e:
        sys.stderr.write(f"bind failed: {e}\n")
        sys.exit(1)
    srv.listen(128)
    srv.setblocking(False)
    sel.register(srv, selectors.EVENT_READ, data="server")

    try:
        while True:
            for key, _ in sel.select(timeout=1.0):
                if key.data == "server":
                    on_acceptable(key.fileobj)
                else:
                    on_readable(key.fileobj)
    except KeyboardInterrupt:
        pass
    finally:
        srv.close()


if __name__ == "__main__":
    main()
