#!/bin/sh
# Runs a command with a deadline: with-timeout.sh SECONDS COMMAND [ARGUMENT...].
# At the deadline the command and its descendants get SIGTERM, then SIGKILL five
# seconds later; the status is 124, as timeout(1) reports it. Descendants are
# found with pgrep -P because a script without a terminal cannot start a
# process group of its own (set -m fails there).
seconds=$1
shift
fired="${TMPDIR:-/tmp}/with-timeout.$$"
rm -f "$fired"

# Sends signal $1 to process $2 and everything below it, children last found first.
signal_tree() {
    for child in $(pgrep -P "$2" 2>/dev/null); do
        signal_tree "$1" "$child"
    done
    kill "-$1" "$2" 2>/dev/null
}

"$@" &
pid=$!
(
    sleep "$seconds"
    : > "$fired"
    signal_tree TERM "$pid"
    sleep 5
    signal_tree KILL "$pid"
) &
watchdog=$!
wait "$pid" 2>/dev/null
status=$?
if [ -e "$fired" ]; then
    # The deadline passed; give the watchdog its SIGKILL pass over stragglers.
    wait "$watchdog" 2>/dev/null
    rm -f "$fired"
    echo "timeout: $* exceeded ${seconds}s" >&2
    exit 124
fi
kill "$watchdog" 2>/dev/null
wait "$watchdog" 2>/dev/null
exit "$status"
