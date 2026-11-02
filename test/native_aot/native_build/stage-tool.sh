#!/bin/sh
# Native build process fixtures. All commands work with an empty PATH.
set -eu
root=${0%/*}
name=${0##*/}
if [ "$#" = 1 ] && [ "$1" = --version ]; then
    case "$name" in
        opt|llc) printf 'LLVM version 22.1.8\n' ;;
        *) printf 'LLD 22.1.8\n' ;;
    esac
    exit 0
fi
trace=$name.argv
case "$name:$*" in
    llc:*.start.ll) trace=$name.start.argv ;;
    *:'-flavor link /lib '*) trace=$name.lib.argv ;;
esac
printf '%s\n' "$@" > "$root/$trace"
case "$name" in
    opt)
        [ "$#" = 4 ] && [ "$2" = -o ] || exit 64
        /bin/cat "$4" > "$3"
        ;;
    llc)
        [ "$#" = 6 ] && [ "$1" = -filetype=obj ] &&
            [ "$3" = -relocation-model=pic ] && [ "$4" = -o ] || exit 64
        printf 'controlled object\n' > "$5"
        case "$6" in *.start.ll) /bin/cp "$6" "$root/start.ll"; exit 0 ;; esac
        IFS= read -r action < "$root/action"
        case "$action" in
            fail) exit 71 ;;
            signal) kill -TERM "$$" ;;
            terminate) kill -TERM "$PPID"; exit 0 ;;
        esac
        ;;
    ld.lld|ld64.lld|lld-link)
        out=
        previous=
        for argument in "$@"; do
            [ "$previous" != -o ] || out=$argument
            case "$argument" in /out:*) out=${argument#/out:} ;; esac
            case "$argument" in
                *.tbd) /bin/cp "$argument" "$root/${argument##*/}" ;;
                /def:*) definition=${argument#/def:}; /bin/cp "$definition" "$root/${definition##*/}" ;;
            esac
            previous=$argument
        done
        [ -n "$out" ] || exit 64
        printf '#!/bin/sh\nexit 42\n' > "$out"
        /bin/chmod 700 "$out"
        ;;
    *) exit 99 ;;
esac
