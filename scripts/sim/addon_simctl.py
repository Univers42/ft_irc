# ---------------------------------------------------------------------------
# addon_simctl.py — HexChat control channel for the ft_irc simulation.
#
# Copied into <cfgdir>/addons/ by hexchat_profile.sh and auto-loaded at
# startup. It watches <cfgdir>/ctl.fifo and runs every line it reads as a
# HexChat command, so the simulation can steer a live GUI client:
#
#     printf 'quote JOIN #x\n'      > <cfgdir>/ctl.fifo   # raw IRC
#     printf 'say hello everyone\n' > <cfgdir>/ctl.fifo   # HexChat command
#
# Two HexChat 2.16 quirks are worked around here; both were found the hard way
# and neither is obvious from the docs:
#
#  1. __file__ is NOT defined for an embedded addon. The config directory has
#     to come from hexchat.get_info("configdir").
#
#  2. hook_timer fires EXACTLY ONCE, whatever the callback returns — returning
#     1 does not keep it armed the way the API documents. The callback
#     therefore re-arms itself on every tick. Remove that and the addon reads
#     the fifo once at startup and then goes deaf forever.
#
# The fifo is opened O_NONBLOCK, so a tick with no writer just reads b"" and
# costs nothing.
# ---------------------------------------------------------------------------
import os

import hexchat

__module_name__ = "simctl"
__module_version__ = "1.0"
__module_description__ = "ft_irc simulation control channel"

INTERVAL_MS = 150

CFGDIR = hexchat.get_info("configdir")
FIFO = os.path.join(CFGDIR, "ctl.fifo")
TRACE = os.path.join(CFGDIR, "simctl.log")

_buf = b""
_fd = None


def _trace(msg):
    try:
        with open(TRACE, "a") as fh:
            fh.write(msg + "\n")
    except IOError:
        pass


def _open():
    """Open the control fifo read-only and non-blocking.

    Read-only O_NONBLOCK succeeds even with no writer attached, and later
    reads simply return b"" until one shows up.
    """
    global _fd
    try:
        _fd = os.open(FIFO, os.O_RDONLY | os.O_NONBLOCK)
    except OSError as exc:
        _fd = None
        _trace("open failed: %s" % exc)


def _context():
    """A context that is actually attached to the server.

    A command run from a timer callback inherits whatever context happens to
    be current, which early in startup is not connected to anything — the
    command is then silently dropped. Prefer the server tab (type 1), fall
    back to any open tab.
    """
    channels = hexchat.get_list("channels")
    for chan in channels:
        if chan.type == 1:
            return chan.context
    return channels[0].context if channels else None


def _rearm():
    hexchat.hook_timer(INTERVAL_MS, _tick)


def _tick(userdata):
    global _buf, _fd

    if _fd is None:
        _open()
        _rearm()
        return 0

    try:
        data = os.read(_fd, 8192)
    except OSError:
        _rearm()
        return 0

    if data:
        _buf += data
        while b"\n" in _buf:
            line, _buf = _buf.split(b"\n", 1)
            command = line.decode("utf-8", "replace").strip()
            if not command:
                continue
            context = _context()
            _trace("run %r (context=%s)" % (command, context is not None))
            if context is not None:
                context.command(command)
            else:
                hexchat.command(command)

    _rearm()
    return 0


_open()
_rearm()
_trace("simctl loaded, watching %s" % FIFO)
