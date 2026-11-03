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
        # The CLI supervises its native host group. This test terminates only
        # its owned processes when a check fails.
        for child in $(ps -axo pid=,ppid= | awk -v parent="$process" '$2 == parent { print $1 }'); do
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
wait_occurrences() {
    attempts=0
    while [ "$(grep -c "$1" "$log" || :)" -lt "$2" ]; do
        kill -0 "$process" 2>/dev/null || fail 'the CLI exited before reporting the accepted edit'
        attempts=$((attempts + 1))
        [ "$attempts" -lt 600 ] || fail 'the accepted edit was not reported'
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
target=x86_64-unknown-linux-gnu
if [ "$(uname -s)" = Darwin ]; then target=arm64-apple-macosx11.0.0; fi
"$driver" run --watch -O0 --llvm "$llvm" --target "$target" "$work/project" >"$log" 2>&1 &
process=$!
live=$work/project/build/native/watch
wait_file "$live/ready"
printf 'package live\nMain :: () -> int { invalid }\n' >"$work/project/Main.chibi"
wait_log 'native: rejected'
kill -0 "$process" 2>/dev/null || fail 'the CLI exited after rejecting an edit'
cat >"$work/project/Main.chibi" <<'SOURCE'
package live
Main :: (value: int) -> int { return value; }
Frame :: () -> int { return 0; }
SOURCE
wait_log 'Main and Frame with a zero-argument int ABI'
kill -0 "$process" 2>/dev/null || fail 'an invalid entry replaced the running program'
cp "$root/test/native_cli/fixtures/watch_middle.chibi" "$work/project/Main.chibi"
wait_file "$live/middle-ready"
cp "$root/test/native_cli/fixtures/watch_second.chibi" "$work/project/Main.chibi"
wait_file "$live/done"
finish 42
[ "$(cat "$live/main-count")" = x ] || fail 'Main ran more than once'
[ "$(cat "$live/task-done")" = old ] || fail 'a retired generation did not complete its task in the old code'
[ ! -f "$live/cancelled-task-ran" ] || fail 'a cancelled task resumed'
printf 'native watch: rejected source/entry, two accepted swaps, record/string migration, Main-once and retired/cancelled tasks passed\n'

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

# Source entries are independently optional. Main runs once without a Frame;
# edits can introduce/remove either entry without restarting that first Main.
mkdir "$work/optional"
cat >"$work/optional/Main.chibi" <<'SOURCE'
package optional_entries
Main :: () { print_string("optional-main-once"); }
SOURCE
log=$work/optional.log
"$driver" run --watch -O0 --llvm "$llvm" "$work/optional" >"$log" 2>&1 &
process=$!
wait_log 'optional-main-once'
printf 'package optional_entries\nMain :: () { invalid }\n' >"$work/optional/Main.chibi"
wait_log 'native: rejected'
cat >"$work/optional/Main.chibi" <<'SOURCE'
package optional_entries
Frame :: () {
    count: retain int = 0;
    count += 1;
    if count == 2 { print_string("optional-frame-ready"); }
}
SOURCE
wait_log 'optional-frame-ready'
cat >"$work/optional/Main.chibi" <<'SOURCE'
package optional_entries
Main :: () { print_string("optional-main-restarted"); }
Frame :: () -> int {
    count: retain int = 0;
    count += 1;
    assert(count >= 3, "Frame-only state migrated");
    shown: retain bool = false;
    if !shown { print_string("optional-retained-ready"); shown = true; }
    return 0;
}
SOURCE
wait_log 'optional-retained-ready'
printf 'package optional_entries\nHelper :: () {}\n' >"$work/optional/Main.chibi"
wait_occurrences 'No runnable Frame' 1
printf 'package optional_entries\n' >"$work/optional/Main.chibi"
wait_occurrences 'No runnable Frame' 2
cat >"$work/optional/Main.chibi" <<'SOURCE'
package optional_entries
Main :: () { print_string("optional-main-restarted"); }
Frame :: () -> int {
    count: retain int = 0;
    count += 1;
    assert(count == 1, "a removed Frame drops its retained state");
    return 42;
}
SOURCE
finish 42
[ "$(grep -c '^optional-main-once$' "$log")" -eq 1 ] || fail 'a Main-only entry did not run exactly once'
if grep -F 'optional-main-restarted' "$log" >/dev/null; then fail 'an edit restarted Main'; fi
printf 'native watch: optional and removed entries preserve Main-once, migrate and reset retained state\n'

# An initially empty package waits for its first runnable entry. That first
# Main runs once; later edits still replace Frame without restarting Main.
mkdir "$work/initially-empty"
printf 'package initially_empty\n' >"$work/initially-empty/Main.chibi"
log=$work/initially-empty.log
"$driver" run --watch -O0 --llvm "$llvm" "$work/initially-empty" >"$log" 2>&1 &
process=$!
wait_log 'No runnable Main'
cat >"$work/initially-empty/Main.chibi" <<'SOURCE'
package initially_empty
Main :: () { print_string("late-main-once"); }
Frame :: () {
    count: retain int = 0;
    count += 1;
    if count == 2 { print_string("late-frame-ready"); }
}
SOURCE
wait_log 'late-frame-ready'
cat >"$work/initially-empty/Main.chibi" <<'SOURCE'
package initially_empty
Main :: () { print_string("late-main-restarted"); }
Frame :: () -> int {
    count: retain int = 0;
    count += 1;
    assert(count >= 3);
    return 7;
}
SOURCE
finish 7
[ "$(grep -c '^late-main-once$' "$log")" -eq 1 ] || fail 'the first runnable Main did not run exactly once'
if grep -F 'late-main-restarted' "$log" >/dev/null; then fail 'a later edit restarted the first runnable Main'; fi
printf 'native watch: an initially empty package starts its first runnable Main once\n'

# Imported names never become root host entries. Their retained keys use the
# stable package identity, rather than package order in a changing closure.
mkdir -p "$work/scoped/dep" "$work/scoped/other"
cat >"$work/scoped/Main.chibi" <<'SOURCE'
package scoped_entries
import dep "project:dep"
Helper :: (value: int) -> int { return value; }
Frame :: () -> int {
    value := Helper(dep.Frame(dep.Main()));
    if value == 2 { print_string("scoped-frame-ready"); }
    return 0;
}
SOURCE
cat >"$work/scoped/dep/Dep.chibi" <<'SOURCE'
package dep
pub Main :: () -> int {
    count: retain int = 0;
    count += 1;
    return count;
}
pub Frame :: (value: int) -> int { return value; }
SOURCE
cat >"$work/scoped/other/Other.chibi" <<'SOURCE'
package other
pub Main :: () -> int { return 123; }
SOURCE
log=$work/scoped.log
"$driver" run --watch -O0 --llvm "$llvm" "$work/scoped" >"$log" 2>&1 &
process=$!
wait_log 'scoped-frame-ready'
cat >"$work/scoped/Main.chibi" <<'SOURCE'
package scoped_entries
import other "project:other"
import dep "project:dep"
Helper :: (value: int) -> int { return value; }
Frame :: () -> int {
    value := Helper(dep.Frame(dep.Main()));
    assert(value >= 3, "imported state survives an earlier package in the closure");
    return 42;
}
SOURCE
finish 42
printf 'native watch: root entries and imported retained identities survive import reordering\n'

# Explicit Frame statuses can equal 128+signal. Only raw cancellation signals
# end an editing session successfully; these ordinary statuses stay unchanged.
mkdir "$work/status"
for expected in 130 143; do
    printf 'package frame_status\nFrame :: () -> int { return %s; }\n' "$expected" >"$work/status/Main.chibi"
    log=$work/status-$expected.log
    "$driver" run --watch -O0 --llvm "$llvm" "$work/status" >"$log" 2>&1 &
    process=$!
    finish "$expected"
done
printf 'native watch: explicit Frame statuses 130 and 143 are preserved\n'

mkdir "$work/arrays"
sed 's/        Ready();/        print_string("array-watch-ready");/' \
    "$root/test/native_aot/generations/ArrayFirst.chibi" > "$work/arrays/Main.chibi"
log=$work/arrays.log
"$driver" run --watch -O0 --llvm "$llvm" "$work/arrays" >"$log" 2>&1 &
process=$!
wait_log 'array-watch-ready'
cp "$root/test/native_aot/generations/ArrayEdited.chibi" "$work/arrays/Main.chibi"
finish 7
printf 'native watch: numeric/bool arrays and record array fields migrate with Main-once\n'
