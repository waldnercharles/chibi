#!/bin/sh
# Isolated real Driver.Native + LLVM + apps/host proof; no SDK asset build or
# language/native-build group is needed. The build/compiler hosts are still C.
set -eu
if [ "$#" -ne 3 ]; then
    echo 'usage: run.sh BUILD_HOST COMPILER_HOST LLVM_BIN' >&2
    exit 2
fi
if [ "$(uname -s)-$(uname -m)" != Linux-x86_64 ]; then
    echo 'numeric package generations require Linux x86-64' >&2
    exit 2
fi
root=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
build_host=$1
compiler_host=$2
llvm=$3
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
    "$build_host" --root "$root/tools/native_aot" \
        --source "$root/tools/native_aot/driver/Driver.chibi" --core "$root/packages/core" \
        --collection "native_link=$root/tools/compiler/native_link" \
        --entry Native --timeout-ms 600000 -- "$compiler_host" "$root/tools/compiler" \
        "$root/packages/core" "$project" "$llvm" 0 none x86_64-unknown-linux-gnu $linkage \
        > "$project/build.log" 2>&1
}
output() {
    sed -n 's/ is up to date$//; s/^native: \(\/.*\)$/\1/p' "$1/build.log" | tail -1
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
    test "$(find "$project/build/native" -name generation.so | wc -l)" -eq 1
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
printf 'package loop\nMain :: () -> int { return 0; }\nFrame :: () -> int { values: int[] = [1, 2]; return length(values); }\n' > "$project/Main.chibi"
rejected 'runtime-free numeric program'
printf 'package loop\nMain :: () -> int { return 0; }\nFrame :: () -> int { return text_byte_length(format_text("{}", int(7))); }\n' > "$project/Main.chibi"
rejected 'runtime-free numeric program'
printf 'package loop\nMain :: () -> int { return 0; }\nFrame :: () -> int { assert(false, "still checked"); return 0; }\n' > "$project/Main.chibi"
rejected 'runtime-free numeric program'

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
echo 'numeric package generations: passed'
