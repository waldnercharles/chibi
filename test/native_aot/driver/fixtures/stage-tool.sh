#!/bin/sh
# Controlled process capabilities, not LLVM implementations. No PATH commands.
set -eu
root=${0%/*}
name=${0##*/}
printf '%s\n' "$@" > "$root/$name.argv"
case "$name" in
    opt*)
        test "$#" = 5 && test "$1" = -S && test "$4" = -o
        printf '%s\n' 'target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"' > "$5"
        /bin/cat "$3" >> "$5"
        ;;
    llc*)
        test "$#" = 5 && test "$1" = -filetype=obj && test "$4" = -o
        IFS= read -r action < "$root/action"
        test "$action" != missing || exit 0
        printf 'controlled object\n' > "$5"
        case "$action" in
            fail) exit 71 ;;
            signal) kill -TERM "$$" ;;
            cancel) kill -USR1 "$PPID"; while :; do /bin/sleep 1; done ;;
            mutate-opt) printf '\n# changed\n' >> "$root/opt tool" ;;
            mutate-llc) printf '\n# changed\n' >> "$0" ;;
        esac
        ;;
    linker*)
        test "$#" = 4 && test "$2" = -Wl,--gc-sections && test "$3" = -o
        printf '#!/bin/sh\nexit 42\n' > "$4"
        /bin/chmod 700 "$4"
        ;;
    *) exit 99 ;;
esac
