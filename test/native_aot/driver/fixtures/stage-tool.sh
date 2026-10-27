#!/bin/sh
# Controlled process capabilities, not LLVM implementations. No PATH commands.
set -eu
root=${0%/*}
name=${0##*/}
trace=$name.argv
case "$name:$*" in
    llc*/start.ll*) trace=$name.start.argv ;;
esac
printf '%s\n' "$@" > "$root/$trace"
case "$name" in
    opt*)
        test "$#" = 5 || exit 64
        test "$1" = -S && test "$4" = -o || exit 64
        # Pass lists stay permissive so independent C argv assertions must catch
        # missing verification/optimization, even when marker emission succeeds.
        printf '%s\n' 'target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"' > "$5"
        /bin/cat "$3" >> "$5"
        ;;
    llc*)
        test "$#" = 6 || exit 64
        test "$1" = -filetype=obj && test "$3" = -relocation-model=pic && test "$5" = -o || exit 64
        if test "${4##*/}" = start.ll; then
            printf 'controlled start object\n' > "$6"
            exit 0
        fi
        IFS= read -r action < "$root/action"
        test "$action" != missing || exit 0
        printf 'controlled object\n' > "$6"
        case "$action" in
            fail) exit 71 ;;
            signal) kill -TERM "$$" ;;
            cancel) kill -USR1 "$PPID"; while :; do /bin/sleep 1; done ;;
            terminate) kill -TERM "$PPID"; while :; do /bin/sleep 1; done ;;
            mutate-opt) printf '\n# changed\n' >> "$root/opt tool" ;;
            mutate-llc) printf '\n# changed\n' >> "$0" ;;
        esac
        ;;
    linker*|ld.lld*)
        test "$#" = 10 || exit 64
        test "$1" = -flavor && test "$3" = -static && test "$5" = -e && test "$9" = -o || exit 64
        printf '#!/bin/sh\nexit 42\n' > "${10}"
        /bin/chmod 700 "${10}"
        ;;
    *) exit 99 ;;
esac
