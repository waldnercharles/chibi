#!/bin/sh
# Native foreign samples share files in their cwd and use the SDK vendor archive.
set -eu
compiler=${1:?usage: samples-native.sh SDK_CLI LLVM_BIN}
llvm=${2:?usage: samples-native.sh SDK_CLI LLVM_BIN}
root=$(cd "$(dirname "$0")/../.." && pwd -P)
compiler=$(cd "$(dirname "$compiler")" && pwd -P)/$(basename "$compiler")
llvm=$(cd "$llvm" && pwd -P)
core=$(cd "$(dirname "$compiler")/../share/chibi/core" && pwd -P) || {
    echo 'native samples require the SDK share/chibi/core' >&2
    exit 2
}
vendor=$(dirname "$compiler")/../lib/chibi-vendor/libchibi_vendor_freetype.a
font=$root/third_party/fonts/inter/Inter.ttf
if [ ! -f "$vendor" ] || [ ! -f "$font" ]; then
    echo 'native samples require the SDK FreeType archive and third_party/fonts/inter/Inter.ttf' >&2
    exit 2
fi
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-native-samples.XXXXXX")
trap 'rm -rf "$work"' EXIT
cp -R "$root/test/compiler/samples/native/." "$work/"
printf 'package fixture\n' > "$work/Main.chibi"
ln -s "$font" "$work/Inter.ttf"
cd "$work"
CHIBI_CORE=$core "$compiler" test --llvm "$llvm" --root "$work" "$work"
