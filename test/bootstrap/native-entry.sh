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
cp "$root/tools/bootstrap/native-seed.sh" "$checkout/tools/bootstrap/native-seed.sh"
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
export ENTRY_COMPILES="$scratch/compiles" ENTRY_RESULT="$scratch/arguments"
cd "$checkout"
checkout=$(pwd -P)
count() { test "$(wc -l < "$ENTRY_COMPILES" | tr -d ' ')" = "$1"; }
entry() { sh tools/build.sh "$@" --native-driver "$checkout/compiler" --llvm "$checkout/llvm"; }

# Cold and warm entry: no bootstrap host, exact argument boundaries, one compile.
entry build test --build 'output with spaces'
printf '%s\n' build test --build 'output with spaces' --native-driver "$checkout/compiler" --llvm "$checkout/llvm" > "$scratch/expected"
cmp "$scratch/expected" "$ENTRY_RESULT"
count 1
entry census
count 1
if (ENTRY_EXIT=17 entry census); then exit 1; else test "$?" = 17; fi
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
if (ENTRY_COMPILE_STATUS=23 entry census); then exit 1; else test "$?" = 23; fi
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
    if (PATH="$scratch/hash-bin:$PATH" ENTRY_REAL_DIGEST=$real_digest entry census); then exit 1; else test "$?" = 37; fi
    count "$next"
    test ! -e .chibi/build-entry/driver
fi
if sh tools/build.sh census --native-driver; then exit 1; else test "$?" = 2; fi
if sh tools/build.sh census --llvm; then exit 1; else test "$?" = 2; fi
if sh tools/build.sh census --native-driver "$checkout/missing" --llvm "$checkout/llvm"; then exit 1; else test "$?" = 2; fi
chmod -x llvm/llc
if entry census; then exit 1; else test "$?" = 2; fi
chmod +x llvm/llc

# Default entry downloads the pinned native seed, verifies it and reuses it.
# The release download boundary is local; the real verifier and entry both run.
mkdir "$scratch/download-bin"
export ENTRY_SEED="$scratch/seed" ENTRY_DOWNLOADS="$scratch/downloads"
cp compiler "$ENTRY_SEED"
seed_hash=$(shasum -a 256 "$ENTRY_SEED" | cut -d' ' -f1)
case "$(uname -s)" in Darwin) platform=macos-arm64 ;; *) platform=linux-x64 ;; esac
printf '%s\tfixture-seed\tchibi-native\t%s\n' "$platform" "$seed_hash" > tools/bootstrap/native-seed.tsv
cat > "$scratch/download-bin/gh" <<'GH'
#!/bin/sh
set -eu
printf 'download\n' >> "$ENTRY_DOWNLOADS"
while [ "$#" -gt 0 ]; do
    case "$1" in --dir) destination=$2 ;; --pattern) asset=$2 ;; esac
    shift
done
cp "$ENTRY_SEED" "$destination/$asset"
GH
chmod +x "$scratch/download-bin/gh"
export PATH="$scratch/download-bin:$PATH"
sh tools/build.sh census --llvm "$checkout/llvm"
next=$((next + 1))
count "$next"
test "$(wc -l < "$ENTRY_DOWNLOADS" | tr -d ' ')" = 1
seed=$checkout/.chibi/native-seed/$platform/chibi
printf '%s\n' build census --llvm "$checkout/llvm" --native-driver "$seed" > "$scratch/expected"
cmp "$scratch/expected" "$ENTRY_RESULT"
# Environment-selected LLVM uses the same cache, and an irrelevant old host
# cannot divert execution back to bytecode.
CHIBI_LLVM_BIN=$checkout/llvm CHIBI_BUILD_HOST=$checkout/missing sh tools/build.sh census
count "$next"
test "$(wc -l < "$ENTRY_DOWNLOADS" | tr -d ' ')" = 1
# Damaged cached bytes are replaced only by a verified download.
printf 'damaged\n' > "$seed"
sh tools/build.sh census --llvm "$checkout/llvm"
count "$next"
test "$(wc -l < "$ENTRY_DOWNLOADS" | tr -d ' ')" = 2
printf 'damaged\n' > "$seed"
printf 'invalid release\n' > "$ENTRY_SEED"
if sh tools/build.sh census --llvm "$checkout/llvm"; then exit 1; else test "$?" = 1; fi
count "$next"
test "$(wc -l < "$ENTRY_DOWNLOADS" | tr -d ' ')" = 3
printf 'native build entry: cold, cached, invalidated, failed and verified default cases passed\n'
