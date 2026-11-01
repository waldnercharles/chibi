#!/bin/sh
# Runs a command while holding one of this machine's heavy-build slots.
#   sh /Users/cwaldner/Personal/chibi/handoffs/heavy.sh "who: what" COMMAND [ARGUMENTS...]
# Waits, without waking the caller, until a slot is free; releases it when the
# command exits, however it exits. A slot whose owner process is gone is taken
# over. Run it in the background and wait for its exit notice: don't poll.
# CHIBI_HEAVY_SLOTS sets the slot count (default 6); CHIBI_HEAVY_PREFIX the
# slot path (default /tmp/chibi-heavy-slot), for testing.
set -u
[ "$#" -ge 2 ] || { echo "usage: heavy.sh OWNER COMMAND [ARGUMENTS...]" >&2; exit 2; }
owner=$1
shift
slots=${CHIBI_HEAVY_SLOTS:-6}
prefix=${CHIBI_HEAVY_PREFIX:-/tmp/chibi-heavy-slot}
held=""
release() {
    [ -n "$held" ] && rm -rf "$held"
    held=""
}
trap release EXIT
trap 'release; exit 130' INT
trap 'release; exit 143' TERM
waited=0
while [ -z "$held" ]; do
    i=0
    while [ "$i" -lt "$slots" ]; do
        slot=$prefix-$i
        [ "$i" -eq 0 ] && slot=$prefix
        if mkdir "$slot" 2>/dev/null; then
            held=$slot
            printf '%s (pid %s, %s)\n' "$owner" "$$" "$(date)" > "$slot/owner"
            break
        fi
        # A slot this script took records its pid; take it over once that
        # process is gone. Slots taken by hand have no pid and are left alone.
        pid=$(sed -n 's/.*(pid \([0-9][0-9]*\),.*/\1/p' "$slot/owner" 2>/dev/null | head -1)
        if [ -n "$pid" ] && ! kill -0 "$pid" 2>/dev/null; then
            rm -rf "$slot"
            continue
        fi
        i=$((i + 1))
    done
    if [ -z "$held" ]; then
        [ "$waited" -eq 0 ] && echo "heavy.sh: all $slots slots busy; waiting" >&2
        waited=1
        sleep 5
    fi
done
[ "$waited" -eq 1 ] && echo "heavy.sh: took $held" >&2
"$@"
