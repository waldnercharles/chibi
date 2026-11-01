#!/bin/sh
# Controlled native stages: these copy scripts, never compile compiler sources.
set -eu
output=
while [ "$#" -gt 0 ]; do
    if [ "$1" = --output ]; then
        output=$2
        shift
    fi
    shift
done
case "$output" in
    */runner/*) echo 'fixture reached runner' >&2; exit 23 ;;
    */stage[234]/chibi) work=${output%/stage?/*} ;;
    *) exit 24 ;;
esac
mode=$(cat "$work/mode")
case "$output:$mode" in
    */stage3/chibi:missing-three|*/stage4/chibi:missing-four) exit 0 ;;
    */stage4/chibi:failed-four) exit 25 ;;
esac
cp "$work/seed" "$output"
case "$output:$mode" in
    */stage2/chibi:equal) ;;
    */stage2/chibi:*|*/stage4/chibi:mismatch) echo '# older compiler output' >> "$output" ;;
esac
chmod +x "$output"
