#!/bin/sh
set -eu
installer=${1:?usage: sdk_wasm_inputs.sh SDK_INSTALLER BUILD_DIRECTORY}
build=${2:?usage: sdk_wasm_inputs.sh SDK_INSTALLER BUILD_DIRECTORY}
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-sdk-wasm-inputs.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work/build/sdk" "$work/prefix"
cp -R "$build/sdk/wasm32" "$work/build/sdk/"
printf 'previous SDK' > "$work/prefix/marker"
printf '\n/* changed */\n' >> "$work/build/sdk/wasm32/asset_compiler_host.h"
set +e
"$installer" --build "$work/build" --prefix "$work/prefix" --with-wasm > "$work/install.log" 2>&1
status=$?
set -e
[ "$status" -eq 1 ]
grep -F 'optional Wasm compiler archives or header are missing or stale' "$work/install.log"
[ "$(cat "$work/prefix/marker")" = 'previous SDK' ]
[ "$(ls -A "$work/prefix")" = marker ]
printf '%s\n' 'SDK Wasm input rejection preserves the existing prefix'
