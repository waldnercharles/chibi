#!/bin/sh
# Exercise the real bootstrap entry without downloading or running a VM host.
# The compiler stub records compiles and emits an executable argument recorder.
set -eu
root=$(cd "$(dirname "$0")/../.." && pwd -P)
scratch=$(mktemp -d "${TMPDIR:-/tmp}/chibi-native-entry.XXXXXX")
trap 'rm -rf "$scratch"' EXIT
checkout=$scratch/'checkout with spaces'
mkdir -p "$checkout/tools/toolchain" "$checkout/tools/bootstrap" "$checkout/packages/core/sample" "$checkout/llvm"
cp "$root/tools/build.sh" "$checkout/tools/build.sh"
printf 'package driver\n' > "$checkout/tools/toolchain/Driver.chibi"
printf 'package sample\n' > "$checkout/packages/core/sample/Sample.chibi"
printf '#!/bin/sh\nexit 93\n' > "$checkout/tools/bootstrap.sh"
printf '#!/bin/sh\nexit 94\n' > "$checkout/tools/bootstrap/seed.sh"
for tool in opt llc ld.lld ld64.lld; do
    printf '#!/bin/sh\nexit 0\n' > "$checkout/llvm/$tool"
    chmod +x "$checkout/llvm/$tool"
done
cat > "$checkout/compiler" <<'COMPILER'
#!/bin/sh
set -eu
printf 'compile\n' >> "$ENTRY_COMPILES"
while [ "$#" -gt 0 ]; do
    if [ "$1" = --output ]; then output=$2; fi
    shift
done
cat > "$output" <<'RUNNER'
#!/bin/sh
printf '%s\n' "$@" > "$ENTRY_RESULT"
exit "${ENTRY_EXIT:-0}"
RUNNER
if [ "${ENTRY_COMPILE_STATUS:-0}" != 0 ]; then exit "$ENTRY_COMPILE_STATUS"; fi
chmod +x "$output"
COMPILER
chmod +x "$checkout/compiler"
export ENTRY_COMPILES=$scratch/compiles ENTRY_RESULT=$scratch/arguments
cd "$checkout"
count() { test "$(wc -l < "$ENTRY_COMPILES" | tr -d ' ')" = "$1"; }
entry() { sh tools/build.sh "$@" --native-driver "$checkout/compiler" --llvm "$checkout/llvm"; }

# Cold and warm entry: no bootstrap host, exact argument boundaries, one compile.
entry build test --build 'output with spaces'
printf '%s\n' build test --build 'output with spaces' --native-driver "$checkout/compiler" --llvm "$checkout/llvm" > "$scratch/expected"
cmp "$scratch/expected" "$ENTRY_RESULT"
count 1
entry census
count 1
if ENTRY_EXIT=17 entry census; then exit 1; else test "$?" = 17; fi
count 1

# Content, not timestamps, determines freshness. Added/deleted core files and
# each tool also invalidate; paths containing spaces remain single arguments.
cp -p tools/toolchain/Driver.chibi "$scratch/original"
printf '// changed\n' >> tools/toolchain/Driver.chibi
touch -r "$scratch/original" tools/toolchain/Driver.chibi
entry census
count 2
printf '// changed\n' >> packages/core/sample/Sample.chibi
entry census
count 3
printf 'package sample\n' > packages/core/sample/Extra.chibi
entry census
count 4
rm packages/core/sample/Extra.chibi
entry census
count 4 # Returns to the exact previously cached input set.
next=5
for tool in compiler llvm/opt llvm/llc llvm/ld.lld llvm/ld64.lld; do
    printf '\n# changed\n' >> "$tool"
    entry census
    # Only this platform's linker contributes to its entry.
    case "$(uname -s)-$tool" in Darwin-llvm/ld.lld|Linux-llvm/ld64.lld) ;;
        *) count "$next"; next=$((next + 1)) ;;
    esac
done

# A failed compile leaves no usable cache, cannot fall back to the C host,
# and a later successful invocation retries instead of using partial output.
printf '\n# failure input\n' >> compiler
if ENTRY_COMPILE_STATUS=23 entry census; then exit 1; else test "$?" = 23; fi
count "$next"
next=$((next + 1))
entry census
count "$next"
entry census
count "$next"
test -z "$(find .chibi/build-entry -maxdepth 1 -name 'work.*' -print)"

# In particular, a failed final digest must not become an empty cache key.
mkdir "$scratch/hash-bin"
real_digest=$(command -v sha256sum || true)
if [ -n "$real_digest" ]; then
    cat > "$scratch/hash-bin/sha256sum" <<'HASH'
#!/bin/sh
case "$1" in */inputs) exit 37 ;; esac
exec "$ENTRY_REAL_DIGEST" "$@"
HASH
    chmod +x "$scratch/hash-bin/sha256sum"
    if PATH="$scratch/hash-bin:$PATH" ENTRY_REAL_DIGEST=$real_digest entry census; then exit 1; else test "$?" = 37; fi
    count "$next"
    test ! -e .chibi/build-entry/driver
fi
if sh tools/build.sh census --native-driver "$checkout/compiler"; then exit 1; else test "$?" = 2; fi
if sh tools/build.sh census --native-driver "$checkout/missing" --llvm "$checkout/llvm"; then exit 1; else test "$?" = 2; fi
chmod -x llvm/llc
if entry census; then exit 1; else test "$?" = 2; fi
chmod +x llvm/llc

# --llvm alone still belongs to the old entry (e.g. native-self-host). The
# caller-supplied host and bytecode seed behavior stay unchanged.
cat > "$checkout/host" <<'HOST'
#!/bin/sh
printf '%s\n' "$@" > "$ENTRY_RESULT"
HOST
chmod +x "$checkout/host"
printf '#!/bin/sh\nprintf seed > "%s"\n' "$scratch/seed-used" > tools/bootstrap/seed.sh
CHIBI_BUILD_HOST=$checkout/host sh tools/build.sh build census --llvm "$checkout/llvm"
test "$(cat "$scratch/seed-used")" = seed
grep -q '^--compiler-core$' "$ENTRY_RESULT"
grep -q '^census$' "$ENTRY_RESULT"
count "$next"
printf 'native build entry: cold, cached, invalidated, failed and legacy cases passed\n'
