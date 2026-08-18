```bash
```
```bash
grep -R -nE '\b(poll|select|epoll_wait|kevent)\s*\(' src/

for f in poll select epoll_wait kevent; do printf "%-12s " "$f"; grep -RhoE "\b$f\s*\(" src/ | wc -l; done

grep -R -nE '\bepoll_wait\s*\(' src/

grep -R -nE '\b(epoll_wait|accept|accept4|read|recv|recvfrom|recvmsg|write|send|sendto|sendmsg)\s*\(' src/
```

grep -R -nE '\b(epoll_wait|poll|select|kevent)\s*\(' src/

grep -R -nE '\b(accept|accept4|read|recv|recvfrom|recvmsg|write|send|sendto|sendmsg)\s*\(' src/

grep -R -nE '\b(EAGAIN|EWOULDBLOCK|errno)\b' src/


```bash
sed -n '100,180p' src/PlatformBus.cpp
sed -n '130,310p' src/Server.cpp


```

## verify that each call to fcntl() is done as follows: fcntl(fd, F_SETFL, O_NONBLOCK) any other use of fcntl() is forbidden

```bash
grep -R -nE '\bf?cntl\s*\(' src/
#for  fcntl specifically

grep -R -nE '\bfcntl\s*\(' src/

grep -R -nE '\bfcntl\s*\(' src/ && grep -R -nE '\bfcntl\s*\([^,]+,[[:space:]]*F_SETFL[[:space:]]*,[[:space:]]*O_NONBLOCK[[:space:]]*\)' src/
```


## Verify port

1. the port is valid/available..
2. The server bidn to all interfaces (0.0.0.0), not just localhost

```bash
ss -ltnp # check port already in use... 
ss -ltnp | grep ':6667' # if nothign returned thisis wrong...


for p in $(seq 6000 8000); do
    if ss -ltn | grep -qE ":$p[[:space:]]"; then
        echo "$p: IN USE"
    else
        echo "$p: AVAILABLE"
    fi
done
```