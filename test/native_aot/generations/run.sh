#!/bin/sh
# Real native compiler + LLVM + apps/host proof, including edits compiled
# while the old generation is running. No bytecode host participates.
set -eu
if [ "$#" -ne 2 ]; then
    echo 'usage: run.sh NATIVE_COMPILER LLVM_BIN' >&2
    exit 2
fi
if [ "$(uname -s)-$(uname -m)" != Linux-x86_64 ]; then
    echo 'native package generations require Linux x86-64' >&2
    exit 2
fi
root=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
compiler=$1
llvm=$2
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
        "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
            --root "$project" --source "$project/Main.chibi" --output "$project/generation.so" \
            --llvm "$llvm" --target x86_64-unknown-linux-gnu --shared -O0 \
            > "$project/build.log" 2>&1
    else
        "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
            --root "$project" --source "$project/Main.chibi" --entry Main --output "$project/host" \
            --llvm "$llvm" --target x86_64-unknown-linux-gnu -O0 \
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
cp "$root/apps/host/Host.chibi" "$work/host/Main.chibi"
must_build "$work/host" ''
host=$(output "$work/host")

swap() {
    first=$1
    edited=$2
    run=$3
    mkdir -p "$run"
    cp "$(output "$first")" "$run/live-0.so"
    (cd "$run" && exec timeout 90 "$host") > "$run/host.log" 2>&1 &
    child=$!
    sleep 0.1
    kill -0 "$child"
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
swap "$work/package first" "$work/package edited" "$work/package run"

# Scalar facts adapt a non-entry void result to int zero, as the library path
# does. Keep typed Main and carry void Frame's state into the typed Frame.
mkdir -p "$work/void first"
sed 's/Frame :: () -> int/Frame :: ()/; /    return 0;/d' \
    "$work/package first/Main.chibi" > "$work/void first/Main.chibi"
must_build "$work/void first" shared
swap "$work/void first" "$work/package edited" "$work/void run"

# Existing library IDs, wrappers and numeric migration take the old path.
for version in first edited; do
    mkdir -p "$work/library $version"
    { echo 'Loop :: library {'; sed '/^package /d' "$work/package $version/Main.chibi"; echo '}'; } \
        > "$work/library $version/Main.chibi"
done
must_build "$work/library first" shared
swap "$work/library first" "$work/library edited" "$work/library run"

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
printf 'package loop\nMain :: () -> int { return 0; }\n' > "$project/Main.chibi"
rejected 'native package generations require Main and Frame'
printf 'package loop\nFrame :: () -> int { return 0; }\n' > "$project/Main.chibi"
rejected 'native package generations require Main and Frame'
printf 'package loop\nMain :: () { }\nFrame :: () -> int { return 0; }\n' > "$project/Main.chibi"
rejected 'scalar proof requires its first function to return int'
printf 'package loop\nFrame :: () -> int { return 0; }\nMain :: (x: int) -> int { return x; }\n' > "$project/Main.chibi"
rejected 'Main and Frame with a zero-argument int ABI'
printf 'package loop\nFrame :: () -> int { return 0; }\nMain :: () -> f64 { return 0.0; }\n' > "$project/Main.chibi"
rejected 'Main and Frame with a zero-argument int ABI'
printf 'package loop\nMain :: () -> int { return 0; }\nFrame :: (x: int) -> int { return x; }\n' > "$project/Main.chibi"
rejected 'Main and Frame with a zero-argument int ABI'
printf 'package loop\nMain :: () -> int { return 0; }\nFrame :: () -> f64 { return 0.0; }\n' > "$project/Main.chibi"
rejected 'Main and Frame with a zero-argument int ABI'
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
sleep 0.1
kill -0 "$child"
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

prior=$(output "$project")
cp "$prior" "$work/prior.so"

# Distinct imported functions with the same short name cannot silently share
# a host wrapper or retained-state key. Existing library uniqueness rejects.
mkdir -p "$project/a" "$project/b"
cat > "$project/Main.chibi" <<'SOURCE'
package loop
import a "project:a"
import b "project:b"
Main :: () -> int { return 0; }
Frame :: () -> int { return a.Value() + b.Value(); }
SOURCE
printf 'package a\npub Value :: () -> int { return 3; }\n' > "$project/a/A.chibi"
printf 'package b\npub Value :: () -> int { return 4; }\n' > "$project/b/B.chibi"
rejected 'native library names each function once'

# Producer-rejected library rows have no DocumentRow. Their existing located
# rejection must survive the shared diagnostic dispatch, without an ABI error.
project="$work/library first"
prior=$(output "$project")
cp "$prior" "$work/prior.so"
printf 'Loop :: library { Main :: () -> int { return Missing(); } Frame :: () -> int { return 0; } }\n' \
    > "$project/Main.chibi"
rejected "Unknown function 'Missing'"
echo 'native package generations: passed'
