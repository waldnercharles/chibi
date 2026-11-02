#!/bin/sh
# Install the native compiler's LLVM 22 tools and expose one directory to every
# build entry in this job. The C bootstrap's clang/libclang is configured apart.
set -eu
: "${GITHUB_ENV:?GITHUB_ENV is required}"
: "${RUNNER_TEMP:?RUNNER_TEMP is required}"
case "$(uname -s)" in
    Linux)
        key=$(mktemp "$RUNNER_TEMP/chibi-llvm-key.XXXXXX")
        trap 'rm -f "$key"' EXIT
        curl --fail --silent --show-error --location --retry 2 --connect-timeout 30 --max-time 60 \
            https://apt.llvm.org/llvm-snapshot.gpg.key --output "$key"
        sudo gpg --dearmor --yes --output /usr/share/keyrings/chibi-llvm.gpg "$key"
        printf '%s\n' 'deb [signed-by=/usr/share/keyrings/chibi-llvm.gpg] https://apt.llvm.org/noble/ llvm-toolchain-noble-22 main' \
            | sudo tee /etc/apt/sources.list.d/chibi-llvm.list > /dev/null
        sudo apt-get update -q
        sudo apt-get install -y -q --no-install-recommends llvm-22 lld-22
        llvm_bin=/usr/lib/llvm-22/bin
        ;;
    Darwin)
        brew install llvm@22 lld@22
        llvm=$(brew --prefix llvm@22)
        lld=$(brew --prefix lld@22)
        llvm_bin=$RUNNER_TEMP/llvm-bin
        mkdir -p "$llvm_bin"
        for tool in opt llc llvm-ar; do
            ln -sf "$llvm/bin/$tool" "$llvm_bin/$tool"
        done
        # Native macOS needs ld64.lld; the other two link cross-target programs.
        for tool in ld.lld ld64.lld lld-link; do
            ln -sf "$lld/bin/$tool" "$llvm_bin/$tool"
        done
        ;;
    *) echo 'native-llvm.sh: only the Linux and macOS CI runners are supported' >&2; exit 2 ;;
esac
"$llvm_bin/opt" --version
"$llvm_bin/llc" --version
printf 'CHIBI_LLVM_BIN=%s\nLLVM_BIN=%s\n' "$llvm_bin" "$llvm_bin" >> "$GITHUB_ENV"
