#!/bin/sh
# Execute the actual start factory IR through the normal LLVM object pipeline.
set -eu
compiler=${1:?pass the native compiler}
llvm=${2:?pass the LLVM directory}
repo=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd -P)
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) target=aarch64-apple-darwin; platform=mac; linker=ld64.lld ;;
    Linux-x86_64) target=x86_64-unknown-linux-gnu; platform=linux; linker=ld.lld ;;
    *) echo 'native exit status: unsupported test host' >&2; exit 2 ;;
esac
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-exit-status.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir "$work/modules" "$work/llvm" "$work/programs"
"$compiler" --compiler "$repo/tools/compiler" --core "$repo/packages/core" \
    --source "$repo/test/native_aot/exit_status/Start.chibi" --entry Main --output "$work/starts" \
    --llvm "$llvm" --target "$target" --level 2 --jobs 1
"$work/starts" "$work/modules"
for source in "$work"/modules/*.ll; do
    "$llvm/opt" -passes=verify -disable-output "$source"
    "$llvm/opt" '-passes=default<O2>' "$source" -o "$source.bc"
    "$llvm/llc" -filetype=obj "$source.bc" -o "$source.o"
done
cat > "$work/llvm/opt" <<'WRAPPER'
#!/bin/sh
set -eu
for source do
    case "$source" in *.ll)
        if grep -Eq '^define (i32 @main\(|void @mainCRTStartup\()|^module asm "_start:' "$source"; then
            kind=plain
            if grep -q '^@chibi_context = global' "$source"; then kind=context; fi
            name="$CHIBI_EXIT_PLATFORM-$kind"
            if grep -q '^module asm "_start:' "$source"; then name=linux-static
            elif grep -q '^define void @mainCRTStartup' "$source"; then name="windows-$kind"; fi
            cp "$CHIBI_EXIT_MODULES/$name.ll" "$source"
        elif [ -n "${CHIBI_EXIT_FAILURE_STUB:-}" ] && grep -q '^define i32 @chibi_scalar(ptr %ctx)' "$source"; then
            cp "$CHIBI_EXIT_FAILURE_STUB" "$source"
        fi ;;
    esac
done
tool=${0##*/}
exec "$CHIBI_EXIT_LLVM/$tool" "$@"
WRAPPER
cp "$work/llvm/opt" "$work/llvm/llc"
chmod +x "$work/llvm/opt" "$work/llvm/llc"
ln -s "$llvm/$linker" "$work/llvm/$linker"
if [ -x "$llvm/lld-link" ]; then ln -s "$llvm/lld-link" "$work/llvm/lld-link"; fi
export CHIBI_EXIT_LLVM=$llvm CHIBI_EXIT_MODULES=$work/modules CHIBI_EXIT_PLATFORM=$platform
"$compiler" --compiler "$repo/tools/compiler" --core "$repo/packages/core" \
    --source "$repo/test/native_cli/ExitStatuses.chibi" --entry Main --output "$work/statuses" \
    --llvm "$llvm" --target "$target" --level 2 --jobs 1
CHIBI_CORE="$repo/packages/core" "$work/statuses" "$compiler" "$work/llvm" "$work/programs"
# A real context failure must precede checking the entry's invalid result.
cat > "$work/failure.ll" <<STUB
target triple = "$target"
@failure = private constant { i64, i32, i32, ptr, i64 } { i64 7, i32 12, i32 19, ptr null, i64 0 }
define i32 @chibi_scalar(ptr %ctx) {
  %slot = getelementptr { ptr, ptr, ptr, ptr, ptr }, ptr %ctx, i32 0, i32 1
  store ptr @failure, ptr %slot
  ret i32 256
}
STUB
printf '%s\n' 'package status' 'Main :: () -> int { values := [int(0)]; return 256 + values[0]; }' > "$work/Failure.chibi"
CHIBI_EXIT_FAILURE_STUB="$work/failure.ll" "$compiler" --compiler "$repo/tools/compiler" --core "$repo/packages/core" \
    --source "$work/Failure.chibi" --entry Main --output "$work/failure" --llvm "$work/llvm" --target "$target" --level 2
set +e
"$work/failure" > "$work/failure.out" 2> "$work/failure.err"
status=$?
set -e
[ "$status" -eq 134 ] || { cat "$work/failure.err"; exit 1; }
if grep -q 'Main returned an exit status' "$work/failure.err"; then cat "$work/failure.err"; exit 1; fi
printf '%s\n' 'PASS start IR targets and context-failure priority'
