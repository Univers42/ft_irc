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