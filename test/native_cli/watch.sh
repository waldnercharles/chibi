#!/bin/sh
# Native CLI -> shared compiler -> live host, with a rejected edit between two
# accepted generations. The running program, rather than a timer, signals readiness.
set -eu
driver=${1:?usage: watch.sh NATIVE_CHIBI LLVM_DIRECTORY}
llvm=${2:?usage: watch.sh NATIVE_CHIBI LLVM_DIRECTORY}
root=$(CDPATH='' cd -- "$(dirname "$0")/../.." && pwd -P)
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-native-watch.XXXXXX")
process=
log=$work/watch.log
cleanup() {
    status=$?
    if [ -n "$process" ] && kill -0 "$process" 2>/dev/null; then
        # The native host shares the CLI's foreground group for Ctrl-C. This
        # test terminates only its owned children when a check fails.
        for child in $(ps -o pid= --ppid "$process"); do
            kill "$child" 2>/dev/null || :
        done
        kill "$process" 2>/dev/null || :
        wait "$process" 2>/dev/null || :
    fi
    if [ "$status" -eq 0 ]; then
        rm -rf "$work"
    else
        cat "$log" >&2 2>/dev/null || :
        printf 'native watch: evidence retained in %s\n' "$work" >&2
    fi
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM
fail() {
    printf 'native watch: %s\n' "$1" >&2
    exit 1
}
wait_file() {
    attempts=0
    while [ ! -f "$1" ]; do
        kill -0 "$process" 2>/dev/null || fail 'the CLI exited before the program signaled readiness'
        attempts=$((attempts + 1))
        [ "$attempts" -lt 600 ] || fail "the program did not create $1"
        sleep 0.1
    done
}
wait_log() {
    attempts=0
    while ! grep "$1" "$log" >/dev/null; do
        kill -0 "$process" 2>/dev/null || fail 'a rejected edit stopped the running program'
        attempts=$((attempts + 1))
        [ "$attempts" -lt 600 ] || fail 'the invalid edit was not diagnosed'
        sleep 0.1
    done
}
finish() {
    attempts=0
    while kill -0 "$process" 2>/dev/null; do
        attempts=$((attempts + 1))
        [ "$attempts" -lt 600 ] || fail 'the CLI did not return the final Frame status'
        sleep 0.1
    done
    set +e
    wait "$process"
    status=$?
    set -e
    process=
    [ "$status" -eq "$1" ] || fail "the final Frame returned $status instead of $1"
}
mkdir "$work/project"
cp "$root/test/native_cli/fixtures/watch_first.chibi" "$work/project/Main.chibi"
export CHIBI_CORE="$root/packages/core"
export CHIBI_LLVM_BIN="$llvm"
export CHIBI_NATIVE_HOST_SOURCE="$root/apps/host/Host.chibi"
"$driver" run --watch -O0 --llvm "$llvm" "$work/project" >"$log" 2>&1 &
process=$!
live=$work/project/build/native/watch
wait_file "$live/ready"
printf 'package live\nMain :: () -> int { invalid }\n' >"$work/project/Main.chibi"
wait_log 'native: rejected'
kill -0 "$process" 2>/dev/null || fail 'the CLI exited after rejecting an edit'
cp "$root/test/native_cli/fixtures/watch_second.chibi" "$work/project/Main.chibi"
wait_file "$live/done"
finish 42
[ "$(cat "$live/main-count")" = x ] || fail 'Main ran more than once'
[ "$(cat "$live/task-done")" = old ] || fail 'a retired generation did not complete its task in the old code'
printf 'native watch: rejected edit, record migration, retained string, Main-once and retired task passed\n'

# Both import failures recover by adding a source inside an existing folder.
# Main is left untouched while the missing source is restored or first created.
mkdir -p "$work/imports/lib" "$work/imports/other"
cat >"$work/imports/Main.chibi" <<'SOURCE'
package watch_imports
import lib "project:lib"
Main :: () -> int { return 0; }
Frame :: () -> int { return lib.Tick(); }
SOURCE
cat >"$work/imports/lib/Lib.chibi" <<'SOURCE'
package lib
foreign import libc "system:c"
foreign libc {
    fopen :: (path: cstring, mode: cstring) -> rawptr
    fwrite :: (data: cstring, size: u64, count: u64, stream: rawptr) -> u64
    fclose :: (stream: rawptr) -> i32
}
pub Tick :: () -> int {
    stream := fopen("import-ready", "wb");
    written := fwrite("ready", 1, 5, stream);
    closed := fclose(stream);
    return 0;
}
SOURCE
log=$work/imports.log
/usr/bin/env PATH= "$driver" run --watch -O0 --llvm "$llvm" "$work/imports" >"$log" 2>&1 &
process=$!
live=$work/imports/build/native/watch
wait_file "$live/import-ready"
rm "$work/imports/lib/Lib.chibi"
wait_log 'package directory has no sources: .*/lib'
sed 's/import-ready/import-restored/' "$root/test/native_cli/fixtures/watch_import.chibi" >"$work/imports/lib/Lib.chibi"
wait_file "$live/import-restored"
cat >"$work/imports/Main.chibi" <<'SOURCE'
package watch_imports
import other "project:other"
Main :: () -> int { return 0; }
Frame :: () -> int { return other.Tick(); }
SOURCE
wait_log 'package directory has no sources: .*/other'
cat >"$work/imports/other/Lib.chibi" <<'SOURCE'
package other
pub Tick :: () -> int { return 7; }
SOURCE
finish 7
printf 'native watch: empty PATH, restored import and newly created import passed\n'
