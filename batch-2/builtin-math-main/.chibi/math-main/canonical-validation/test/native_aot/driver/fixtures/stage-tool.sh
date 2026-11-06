#!/bin/sh
# Controlled process capabilities, not LLVM implementations. No PATH commands.
set -eu
root=${0%/*}
name=${0##*/}
trace=$name.argv
case "$name:$*" in
    llc*/start.ll*) trace=$name.start.argv ;;
    *:"-flavor link /lib "*) trace=$name.lib.argv ;;
esac
printf '%s\n' "$@" > "$root/$trace"
case "$name" in
    opt*)
        test "$#" = 5 || exit 64
        test "$1" = -S && test "$4" = -o || exit 64
        # Pass lists stay permissive so independent C argv assertions must catch
        # missing verification/optimization, even when marker emission succeeds.
        # The layout LLVM 18 gives the input's target, so the driver admits it.
        layout='e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128'
        case "$(/bin/cat "$3")" in
            *'"arm64-apple-macosx11.0.0"'*) layout='e-m:o-i64:64-i128:128-n32:64-S128' ;;
            *'"x86_64-pc-windows-msvc"'*) layout='e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128' ;;
        esac
        printf 'target datalayout = "%s"\n' "$layout" > "$5"
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
        test "$1" = -flavor || exit 64
        case "$2:$#" in
            gnu:10)
                test "$3" = -static && test "$5" = -e && test "$9" = -o || exit 64
                out=${10}
                ;;
            darwin:16)
                test "$3" = -arch && test "${15}" = -o || exit 64
                out=${16}
                ;;
            link:7 | link:11)
                for argument in "$@"; do
                    case "$argument" in /out:*) out=${argument#/out:} ;; esac
                done
                ;;
            *) exit 64 ;;
        esac
        printf '#!/bin/sh\nexit 42\n' > "$out"
        /bin/chmod 700 "$out"
        ;;
    *) exit 99 ;;
esac
