#!/bin/sh
set -eu
compiler=${1:?native compiler}
llvm=${2:?native LLVM}
audit_llvm=${3:?LLVM with the task ownership audit wrapper}
work=${4:?owned task test workspace}/generations
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) target=aarch64-apple-darwin ;;
    Linux-x86_64) target=x86_64-unknown-linux-gnu ;;
    *) exit 77 ;;
esac
mkdir "$work"
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/apps/host/Host.chibi" --entry Main --output "$work/host" \
    --llvm "$llvm" --target "$target" --level 2 --jobs 2
child=
cleanup() {
    if [ -n "$child" ]; then kill "$child" 2>/dev/null || :; wait "$child" 2>/dev/null || :; fi
}
trap cleanup EXIT HUP INT TERM
for level in 0 2; do
    for source in Generation Edited ResizeGeneration TypedGeneration NestedPointers; do
        "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
            --source "$root/test/native_aot/task_owners/$source.chibi" --shared \
            --output "$work/$source-$level.so" --llvm "$audit_llvm" --target "$target" --level "$level" --jobs 1
    done
    for mode in complete cancel failure resize resize-functional unwrap unwrap-await unwrap-immediate nested-pointers; do
        run="$work/$mode-O$level"
        mkdir "$run"
        if [ "$mode" != complete ]; then touch "$run/$mode-case"; fi
        source=Generation
        case "$mode" in resize*) source=ResizeGeneration ;; unwrap*) source=TypedGeneration ;; nested-pointers) source=NestedPointers ;; esac
        cp "$work/$source-$level.so" "$run/live-0.so"
        (cd "$run" && exec timeout 90 "$work/host") > "$run/host.log" 2>&1 &
        child=$!
        attempts=0
        while [ ! -e "$run/owner-ready" ]; do
            if ! kill -0 "$child" 2>/dev/null; then cat "$run/host.log"; exit 1; fi
            attempts=$((attempts + 1))
            if [ "$attempts" -ge 1000 ]; then cat "$run/host.log"; exit 1; fi
            sleep 0.01
        done
        cp "$work/Edited-$level.so" "$run/pending.so"
        mv "$run/pending.so" "$run/next.so"
        result=0
        wait "$child" || result=$?
        child=
        expected=7
        case "$mode" in failure|resize*|unwrap*) expected=134 ;; esac
        if [ "$result" -ne "$expected" ]; then cat "$run/host.log"; echo "owned generation $mode O$level status$result"; exit 1; fi
        if [ "$mode" = complete ]; then [ "$(cat "$run/old-done")" = old:48 ]; fi
        if [ "$mode" = nested-pointers ]; then test -e "$run/old-done"; fi
        case "$mode" in unwrap*)
            grep -Fx 'native failure: UnwrappedNil' "$run/host.log"
            test ! -e "$run/after-unwrap"
            test ! -e "$run/after-await" ;;
        esac
        printf 'PASS owned old generation %s O%s, image close balanced\n' "$mode" "$level"
    done
    for mode in positive sync frame spawn-immediate; do
        run="$work/unwrap-$mode-O$level"
        mkdir "$run"
        touch "$run/$mode-case"
        cp "$work/TypedGeneration-$level.so" "$run/live-0.so"
        result=0
        (cd "$run" && timeout 90 "$work/host") > "$run/host.log" 2>&1 || result=$?
        expected=134
        if [ "$mode" = positive ]; then expected=42; fi
        if [ "$result" -ne "$expected" ]; then cat "$run/host.log"; echo "shared unwrap $mode O$level status$result"; exit 1; fi
        if [ "$mode" = positive ]; then test -e "$run/after-unwrap"
        else
            grep -Fx 'native failure: UnwrappedNil' "$run/host.log"
            test ! -e "$run/after-unwrap"
            test ! -e "$run/after-spawn"
        fi
        printf 'PASS shared unwrap %s O%s, image close balanced\n' "$mode" "$level"
    done
done

# Prove the close-time audit actually runs. A phantom outstanding allocation
# must trap when the host retires the old image, rather than pass unnoticed.
mkdir "$work/bad-audit" "$work/bad-close"
sed 's/@task_owner.shared.live = internal global i64 0/@task_owner.shared.live = internal global i64 1/' \
    "$root/test/native_aot/task_owners/SharedAudit.ll" > "$work/bad-audit/SharedAudit.ll"
CHIBI_TASK_OWNER_AUDIT="$work/bad-audit" "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/task_owners/Generation.chibi" --shared --output "$work/bad-close/live-0.so" \
    --llvm "$audit_llvm" --target "$target" --level 0 --jobs 1
cp "$work/Edited-0.so" "$work/bad-close/next.so"
touch "$work/bad-close/cancel-case"
result=0
(cd "$work/bad-close" && timeout 90 "$work/host") > "$work/bad-close/host.log" 2>&1 || result=$?
case "$result" in
    132|133) ;;
    *) cat "$work/bad-close/host.log"; echo "close audit failed to detect its control, status$result"; exit 1 ;;
esac
printf 'PASS image-close audit rejects a deliberately unbalanced owner\n'
