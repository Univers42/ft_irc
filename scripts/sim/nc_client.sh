#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# nc_client.sh <host> <port> <fifo> <raw-log> <rx-log>
#
# One simulated netcat client. Reads commands from <fifo>, writes two logs:
#
#   <raw-log>  exact bytes the server sent, CR included — this is the one to
#              grep when you care about the protocol.
#   <rx-log>   the same lines with a timestamp and the CR stripped — this is
#              the one to read.
#
# The timestamp uses bash's printf '%(...)T' BUILTIN. $(date) here would fork
# once per received line, which collapses the moment a channel gets busy.
# ---------------------------------------------------------------------------
set -u

host="$1"; port="$2"; fifo="$3"; raw="$4"; rx="$5"

nc "$host" "$port" < "$fifo" 2>/dev/null | tee -a "$raw" | \
while IFS= read -r line; do
    printf '%(%H:%M:%S)T %s\n' -1 "${line%$'\r'}"
done >> "$rx"
