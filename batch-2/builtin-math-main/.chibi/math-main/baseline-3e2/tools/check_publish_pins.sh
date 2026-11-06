#!/bin/sh
# Fail before installing toolchains when the SDK's release inputs are unpinned.
set -eu

manifest=${1:-third_party/archives.tsv}
failed=0
for target in linux-x64 web web-player; do
    if ! awk -F '\t' -v target="$target" '
        $1 == target && $2 == "release" {
            count++
            if (NF != 4 || $3 == "" || $3 ~ /[[:space:]]/ ||
                length($4) != 64 || $4 ~ /[^0-9a-f]/) invalid = 1
        }
        END { exit (count != 1 || invalid) }
    ' "$manifest"; then
        printf '::error file=third_party/archives.tsv::Expected one valid %s release pin.\n' "$target" >&2
        failed=1
    fi
done
if [ "$failed" -ne 0 ]; then
    echo 'Run the Third-party workflow and merge its archive-pin PR before retrying SDK publication.' >&2
    exit 1
fi
