#!/bin/sh
# Real native compiler + LLVM + apps/host proof, including edits compiled
# while the old generation is running. No bytecode host participates.
set -eu
if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
    echo 'usage: run.sh NATIVE_COMPILER LLVM_BIN [0|2]' >&2
    exit 2
fi
case "$(uname -s)-$(uname -m)" in
    Linux-x86_64) triple=x86_64-unknown-linux-gnu ;;
    Darwin-arm64) triple=aarch64-apple-darwin ;;
    *) echo 'native package generations require Linux x86-64 or macOS arm64' >&2; exit 2 ;;
esac
root=$(CDPATH='' cd -- "$(dirname "$0")/../../.." && pwd -P)
compiler=$1
llvm=$2
level=${3:-0}
case "$level" in 0|2) ;; *) echo 'native generations: optimization must be 0 or 2' >&2; exit 2 ;; esac
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-generations.XXXXXX")
child=
cleanup() {
    if [ -n "$child" ]; then
        kill "$child" 2>/dev/null || :
        wait "$child" 2>/dev/null || :
    fi
    rm -rf "$work"
}
trap cleanup EXIT HUP INT TERM

build() {
    project=$1
    linkage=$2
    if [ "$linkage" = shared ]; then
        "$compiler" --compiler "$(dirname "$project")" --core "$root/packages/core" \
            --source "$project/Main.chibi" --output "$project/generation.so" \
            --llvm "$llvm" --target "$triple" --shared --level "$level" \
            > "$project/build.log" 2>&1
    else
        "$compiler" --compiler "$(dirname "$project")" --core "$root/packages/core" \
            --source "$project/Main.chibi" --entry Main --output "$project/host" \
            --llvm "$llvm" --target "$triple" --level "$level" \
            > "$project/build.log" 2>&1
    fi
}
output() {
    if [ -f "$1/host" ]; then printf '%s/host\n' "$1";
    else printf '%s/generation.so\n' "$1"; fi
}
must_build() {
    if ! build "$1" "$2"; then
        cat "$1/build.log" >&2
        exit 1
    fi
}

# First build a package, so unchanged code is a meaningful negative control:
# it rejects this actual retained program instead of emitting a generation.
mkdir -p "$work/package first" "$work/package edited" "$work/host"
cp "$root/test/native_aot/generations/First.chibi" "$work/package first/Main.chibi"
cp "$root/test/native_aot/generations/Edited.chibi" "$work/package edited/Main.chibi"
must_build "$work/package first" shared
if ! "$compiler" --compiler "$root/apps/host" --core "$root/packages/core" \
    --source "$root/apps/host/Host.chibi" --entry Main --output "$work/host/host" \
    --llvm "$llvm" --target "$triple" --level "$level" > "$work/host/build.log" 2>&1; then
    cat "$work/host/build.log" >&2
    exit 1
fi
host=$(output "$work/host")

wait_marker() {
    marker=$1
    host_log=$2
    tries=0
    while [ ! -f "$marker" ]; do
        if ! kill -0 "$child" 2>/dev/null; then
            cat "$host_log" >&2
            echo "native generations: host exited before $marker" >&2
            exit 1
        fi
        tries=$((tries + 1))
        if [ "$tries" -ge 1000 ]; then
            cat "$host_log" >&2
            echo "native generations: missing $marker" >&2
            exit 1
        fi
        sleep 0.01
    done
}

swap() {
    first=$1
    edited=$2
    run=$3
    marker=$4
    mkdir -p "$run"
    cp "$(output "$first")" "$run/live-0.so"
    (cd "$run" && exec timeout 90 "$host") > "$run/host.log" 2>&1 &
    child=$!
    if [ -n "$marker" ]; then wait_marker "$run/$marker" "$run/host.log"; fi
    must_build "$edited" shared
    cp "$(output "$edited")" "$run/pending.so"
    mv "$run/pending.so" "$run/next.so"
    status=0
    wait "$child" || status=$?
    child=
    if [ "$status" -ne 7 ]; then
        cat "$run/host.log" >&2
        echo "generation swap: expected 7, got $status" >&2
        exit 1
    fi
}
swap "$work/package first" "$work/package edited" "$work/package run" generation-ready

# Scalar facts adapt a non-entry void result to int zero, as the library path
# does. Keep typed Main and carry void Frame's state into the typed Frame.
mkdir -p "$work/void first"
sed 's/Frame :: () -> int/Frame :: ()/; /    return 0;/d' \
    "$work/package first/Main.chibi" > "$work/void first/Main.chibi"
must_build "$work/void first" shared
swap "$work/void first" "$work/package edited" "$work/void run" generation-ready

# Existing library IDs, wrappers and numeric migration take the old path.
mkdir -p "$work/library first" "$work/library edited"
cp "$root/test/native_aot/generations/LibraryFirst.chibi" "$work/library first/Main.chibi"
{ echo 'Loop :: library {'; sed '/^package /d' "$work/package edited/Main.chibi"; echo '}'; } \
    > "$work/library edited/Main.chibi"
must_build "$work/library first" shared
swap "$work/library first" "$work/library edited" "$work/library run" ''

# A sibling import must use package collection, not the executable-only fast
# driver. Its public helper is called from Frame in the resulting shared object.
mkdir -p "$work/imports/game" "$work/imports/math" "$work/import run"
cat > "$work/imports/game/Main.chibi" <<'SOURCE'
package game
import math "project:math"
Main :: () -> int { return 0; }
Frame :: () -> int { return math.Value(); }
SOURCE
printf 'package math\npub Value :: () -> int { return 7; }\n' > "$work/imports/math/Math.chibi"
must_build "$work/imports/game" shared
cp "$(output "$work/imports/game")" "$work/import run/live-0.so"
status=0
(cd "$work/import run" && timeout 90 "$host") || status=$?
test "$status" -eq 7

# A void Main is called once and adapts to zero status; Frame still controls exit.
mkdir -p "$work/void main" "$work/void run"
printf 'package loop\nMain :: () { calls: retain int = 0; calls += 1; assert(calls == 1); }\nFrame :: () -> int { return 7; }\n' > "$work/void main/Main.chibi"
must_build "$work/void main" shared
cp "$(output "$work/void main")" "$work/void run/live-0.so"
status=0
(cd "$work/void run" && timeout 90 "$host") || status=$?
test "$status" -eq 7

# The public native build route accepts the versioned macOS ABI triple too.
# Its target constants must follow the emitted image, rather than the build host.
if [ "$triple" = aarch64-apple-darwin ]; then
    canonical="$work/canonical target"
    mkdir -p "$canonical" "$work/canonical run"
    cat > "$canonical/Main.chibi" <<'SOURCE'
package canonical_target
import target "core:target"
Main :: () -> int {
    assert(target.Os == target.Darwin && target.Arch == target.Arm64 && target.PointerBits == 64);
    return 0;
}
Frame :: () -> int { return 7; }
SOURCE
    native_output=$(env -i PATH= CHIBI_CORE="$root/packages/core" "$compiler" build --native --shared -O2 \
        --llvm "$llvm" --target arm64-apple-macosx11.0.0 "$canonical" 2>&1)
    native_image=$(printf '%s\n' "$native_output" | sed -n 's/^native: //p')
    test -f "$native_image"
    cp "$native_image" "$work/canonical run/live-0.so"
    status=0
    (cd "$work/canonical run" && timeout 30 "$host") || status=$?
    test "$status" -eq 7
fi

# Invalid edits preserve the already published generation byte for byte and
# publish no second generation. Test real diagnostics, not just nonzero status.
project="$work/package first"
prior=$(output "$project")
cp "$prior" "$work/prior.so"
rejected() {
    expected=$1
    if build "$project" shared; then
        echo "unexpected generation acceptance: $expected" >&2
        exit 1
    fi
    if ! grep -F "$expected" "$project/build.log"; then
        cat "$project/build.log" >&2
        exit 1
    fi
    cmp "$prior" "$work/prior.so"
}
printf 'package loop\nHelper :: () -> int { return 0; }\n' > "$project/Main.chibi"
rejected 'native package generations require Main or Frame'
printf 'package loop\nFrame :: () -> int { return 0; }\nMain :: (x: int) -> int { return x; }\n' > "$project/Main.chibi"
rejected 'Main and Frame with a zero-argument int ABI'
printf 'package loop\nFrame :: () -> int { return 0; }\nMain :: () -> f64 { return 0.0; }\n' > "$project/Main.chibi"
rejected 'Main and Frame with a zero-argument int ABI'
printf 'package loop\nMain :: () -> int { return 0; }\nFrame :: (x: int) -> int { return x; }\n' > "$project/Main.chibi"
rejected 'Main and Frame with a zero-argument int ABI'
printf 'package loop\nMain :: () -> int { return 0; }\nFrame :: () -> f64 { return 0.0; }\n' > "$project/Main.chibi"
rejected 'Main and Frame with a zero-argument int ABI'
printf 'package loop\nMain :: () -> int { yield\n return 0; }\nFrame :: () -> int { return 0; }\n' > "$project/Main.chibi"
rejected 'native package generation Main and Frame cannot wait'
printf 'package loop\nMain :: () -> int { return 0; }\nFrame :: () -> int { yield\n return 0; }\n' > "$project/Main.chibi"
rejected 'native package generation Main and Frame cannot wait'

# Runtime-backed generations keep assertions enabled and can allocate arrays
# and format strings through their own libc-backed context.
expect_status() {
    expected=$1
    must_build "$project" shared
    run="$work/runtime-$expected"
    mkdir -p "$run"
    cp "$(output "$project")" "$run/live-0.so"
    status=0
    (cd "$run" && timeout 30 "$host") || status=$?
    test "$status" -eq "$expected"
}
# A task started by Main must advance before any edit is published.
cp "$root/test/native_aot/native_enc/ActiveTasks.chibi" "$project/Main.chibi"
expect_status 7
cp "$root/test/native_aot/native_enc/ActiveTaskFailure.chibi" "$project/Main.chibi"
expect_status 134

printf 'package loop\nMain :: () -> int { return 0; }\nFrame :: () -> int { values: int[] = [1, 2]; return length(values); }\n' > "$project/Main.chibi"
expect_status 2
printf 'package loop\nMain :: () -> int { return 0; }\nFrame :: () -> int { return text_byte_length(format_text("{}", int(7))); }\n' > "$project/Main.chibi"
expect_status 1
printf 'package loop\nMain :: () -> int { return 0; }\nFrame :: () -> int { assert(false, "still checked"); return 0; }\n' > "$project/Main.chibi"
expect_status 134

# Recursive calls keep the inner call's retained writes. A blanket writeback
# of every cached local on the outer return would reset the shared counter.
cat > "$project/Main.chibi" <<'SOURCE'
package loop
Main :: () -> int { return 0; }
Frame :: () -> int { first := Step(); return Step(); }
Step :: () -> int {
    count: retain int = 0;
    count += 1;
    if count == 1 { ignored := Step(); }
    return count;
}
SOURCE
expect_status 3

# Readonly scoped parameters lower by value. A recursive state write reached
# through that reader must not be overwritten by the outer cached record.
cat > "$project/Main.chibi" <<'SOURCE'
package loop
Counter :: struct { count: int; }
Main :: () -> int { return 0; }
Frame :: () -> int { first := Step(); return Step(); }
Step :: () -> int {
    counter: retain Counter = {count: 0};
    counter.count += 1;
    if counter.count == 1 { ignored := Observe(&counter); }
    return counter.count;
}
Observe :: (value: ^Counter) -> int { ignored := Step(); return value.count; }
SOURCE
expect_status 3

# Passing a borrowed string parameter by reference promotes its local copy,
# so replacement inside the nested call does not release the caller's bytes.
cat > "$project/Main.chibi" <<'SOURCE'
package loop
Main :: () -> int { return 0; }
Frame :: () -> int {
    original := format_text("{}", int(12345));
    changed := Outer(original);
    churn := format_text("{}", int(99999));
    if original != "12345" { return 8; }
    return changed;
}
Outer :: (text: string) -> int { changed := Replace(&text); return text_byte_length(text); }
Replace :: (text: ^string) -> int { text^ = format_text("{}", int(777)); return 0; }
SOURCE
expect_status 3

# Every generation sees the host's process arguments and environment through
# the same self symbols core:os uses.
cat > "$project/Main.chibi" <<'SOURCE'
package loop
foreign {
    chibi_argument_count :: () -> i64
    chibi_environment :: () -> rawptr
}
Main :: () -> int { return 0; }
Frame :: () -> int {
    if chibi_argument_count() > 0 && chibi_environment() != 0 { return 7; }
    return 8;
}
SOURCE
expect_status 7

# Record fields migrate by name across reordering, and new fields use their
# own initializer. Retained strings must survive the old mapping's unload.
# Compile both edits while the first generation is running. A string/scalar
# schema mismatch must leave it running so a later valid edit can apply.
mkdir -p "$work/composite first" "$work/composite edited" "$work/composite bad" "$work/composite run"
cp "$root/test/native_aot/native_enc/First.chibi" "$work/composite first/Main.chibi"
cp "$root/test/native_aot/native_enc/Incompatible.chibi" "$work/composite bad/Main.chibi"
must_build "$work/composite first" shared
cp "$(output "$work/composite first")" "$work/composite run/live-0.so"
(cd "$work/composite run" && exec timeout 90 "$host") > "$work/composite run/host.log" 2>&1 &
child=$!
wait_marker "$work/composite run/generation-ready" "$work/composite run/host.log"
must_build "$work/composite bad" shared
cp "$(output "$work/composite bad")" "$work/composite run/pending.so"
mv "$work/composite run/pending.so" "$work/composite run/next.so"
tries=0
while [ ! -f "$work/composite run/rejected.so" ] && [ "$tries" -lt 100 ]; do
    kill -0 "$child"
    sleep 0.01
    tries=$((tries + 1))
done
test -f "$work/composite run/rejected.so"
kill -0 "$child"
cp "$root/test/native_aot/native_enc/Edited.chibi" "$work/composite edited/Main.chibi"
must_build "$work/composite edited" shared
cp "$(output "$work/composite edited")" "$work/composite run/pending.so"
mv "$work/composite run/pending.so" "$work/composite run/next.so"
status=0
wait "$child" || status=$?
child=
if [ "$status" -ne 7 ]; then
    cat "$work/composite run/host.log" >&2
    echo "composite generation swap: expected 7, got $status" >&2
    exit 1
fi

# A suspended task belongs to its old generation after a swap. Release it
# only after the incoming Frame runs, then require its old-only helper failure
# to stop the host with 134 rather than disappear when that generation retires.
mkdir -p "$work/task first" "$work/task edited" "$work/task run"
cp "$root/test/native_aot/generations/TaskFailureFirst.chibi" "$work/task first/Main.chibi"
must_build "$work/task first" shared
cp "$(output "$work/task first")" "$work/task run/live-0.so"
(cd "$work/task run" && exec timeout 90 "$host") > "$work/task run/host.log" 2>&1 &
child=$!
wait_marker "$work/task run/task-started" "$work/task run/host.log"
cp "$root/test/native_aot/generations/TaskFailureEdited.chibi" "$work/task edited/Main.chibi"
must_build "$work/task edited" shared
cp "$(output "$work/task edited")" "$work/task run/pending.so"
mv "$work/task run/pending.so" "$work/task run/next.so"
wait_marker "$work/task run/task-edited" "$work/task run/host.log"
kill -0 "$child"
test ! -e "$work/task run/task-failed-old"
printf 'resume\n' > "$work/task run/task-release"
status=0
wait "$child" || status=$?
child=
if [ "$status" -ne 134 ] || [ ! -f "$work/task run/task-failed-old" ]; then
    cat "$work/task run/host.log" >&2
    echo "retired task failure: expected old task marker and status 134, got $status" >&2
    exit 1
fi
test "$(cat "$work/task run/task-failed-old")" = old
echo 'native generations: retired task failure propagated'

prior=$(output "$project")
cp "$prior" "$work/prior.so"

# Distinct imported functions with the same short name retain their package
# identity, so their host wrappers and retained-state keys stay separate.
mkdir -p "$(dirname "$project")/a" "$(dirname "$project")/b"
cat > "$project/Main.chibi" <<'SOURCE'
package loop
import a "project:a"
import b "project:b"
Main :: () -> int { return 0; }
Frame :: () -> int { return a.Value() + b.Value(); }
SOURCE
printf 'package a\npub Value :: () -> int { count: retain int = 3; return count; }\n' > "$(dirname "$project")/a/A.chibi"
printf 'package b\npub Value :: () -> int { count: retain int = 4; return count; }\n' > "$(dirname "$project")/b/B.chibi"
expect_status 7

# Producer-rejected library rows have no DocumentRow. Their existing located
# rejection must survive the shared diagnostic dispatch, without an ABI error.
project="$work/library first"
prior=$(output "$project")
cp "$prior" "$work/prior.so"
printf 'Loop :: library { Main :: () -> int { return Missing(); } Frame :: () -> int { return 0; } }\n' \
    > "$project/Main.chibi"
rejected "Unknown function 'Missing'"

# The legacy bulk word ABI cannot carry borrowed string descriptors. Reject
# that library form while preserving its previously published numeric image.
printf 'Loop :: library { Frame :: () -> int { text: retain string = "unsupported bulk handle"; return 0; } }\n' \
    > "$project/Main.chibi"
rejected 'native library retained states require scalar literal initializers'
echo 'native package generations: passed'
