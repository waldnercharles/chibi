#!/bin/sh
# Runs a command with a deadline: with-timeout.sh SECONDS COMMAND [ARGUMENT...].
# At the deadline the command's process group gets SIGTERM, then SIGKILL five
# seconds later; the status is 124, as timeout(1) reports it. The command runs
# in a group of its own, so a hung test's children stop with it.
seconds=$1
shift
fired="${TMPDIR:-/tmp}/with-timeout.$$"
rm -f "$fired"
set -m
"$@" &
pid=$!
(
    sleep "$seconds"
    : > "$fired"
    kill -TERM -- "-$pid" 2>/dev/null
    sleep 5
    kill -KILL -- "-$pid" 2>/dev/null
) &
watchdog=$!
wait "$pid" 2>/dev/null
status=$?
kill "$watchdog" 2>/dev/null
wait "$watchdog" 2>/dev/null
if [ -e "$fired" ]; then
    rm -f "$fired"
    echo "timeout: $* exceeded ${seconds}s" >&2
    exit 124
fi
exit "$status"
