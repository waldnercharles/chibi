#!/bin/bash
# Automatic activation at repository entry points, never a platform shell hook.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd -P)
prefix=${CHIBI_CLOUD_TOOLS:-/workspace/.cloud-tools}
enabled() {
    [[ ${CHIBI_CLOUD:-auto} != 0 && -d $prefix ]] || return 1
    [[ -r /etc/os-release ]] || return 1
    . /etc/os-release
    [[ ${ID-} == debian && ${VERSION_ID-} == 13 && $(uname -m) == x86_64 ]]
}
if [[ ${1-} == --probe ]]; then
    if enabled && [[ ${CHIBI_CLOUD_ACTIVE-} != "$prefix" ]]; then printf '1\n'; fi
    exit 0
fi
if [[ ${1-} == --report ]]; then
    if enabled; then exec "$repo/tools/cloud/bin/start_skill"; fi
    echo 'Cloud activation skipped (opt-out, no prefix, or unsupported host).'
    exit 0
fi
(( $# )) || { echo 'Usage: entry.sh [--probe|--report|command args...]' >&2; exit 2; }
if enabled && [[ ${CHIBI_CLOUD_ACTIVE-} != "$prefix" ]]; then
    exec "$repo/tools/cloud/bin/start_skill" -- "$@"
fi
exec "$@"
