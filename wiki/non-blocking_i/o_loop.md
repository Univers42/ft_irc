42 school tell us to build the server around an `event-driven, non-blocking I/O loop`, rather than creating a process or thread per client.

The key implications are:
- No `fork()`: The IRC server should run as a single process.
- **All socket I/O must be non-bloking**: listening socket and client sockets should use non-blocking mode.
- User `poll()` (typically the expected choice for ft_irc) to monitor:
    - the server socket for new connections
    - cient sockets for incoming data
    - client socekts that are ready to send pending output.
- Never assume `recv()` or `send()` completes everything in one call.
- keep buffers:
    - an **input buffer per client** for partial IRC messages
    - an output buffer/queue per client for data that could not yet be fully sent

```bash
while (server is running)
{
    poll(all sockets);
    if (server socket has POLLIN)
        accept all available clients;
    for (each client) {
        if (client has POLLIN)
            read available data into its input buffer;
        process complete IRC commands form the input buffer;
        if (client has POLLOUT)
            send as much of its pending output as possible;
        if (error/disconnect)
            remove client;
    }
}

```

The particularly important part is that "non-blocking" doesn't mean we can ignore erros. For example, `recv()` or `send()` may return an error such as `EAGAIN`/`EWOULDBLOCK`, which usually means:

> The operation cannot process right now; return to the event loop and try again when `pool()` says the socket is ready

Also, with IRC messages, TCP is a **byte stream**, so one `recv()` might contain:
- half of a command
- exactly one command,
- or several commands at once

That's why per-client buffering is essential.


# To verify that the server does not fork

We first run the server:



```bash
    strace -fe trace=process ./build/bin/ircserv 6667 pass
```

if the program calls `fork()`, `clone()`, etc. we'll see it.
