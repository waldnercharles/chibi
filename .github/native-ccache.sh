#!/bin/sh
# Configures this job's ccache store and names the snapshots worth restoring.
# ccache keys every object by its compiler's content and flags, so the prefixes
# only choose the snapshot most likely to hit.
set -eu
config="$RUNNER_TEMP/chibi-ccache.conf"
printf 'max_size = 500M\ncompiler_check = content\nhash_dir = true\nsloppiness =\n' > "$config"
{
    echo "CCACHE_DIR=$RUNNER_TEMP/chibi-ccache"
    echo "CCACHE_CONFIGPATH=$config"
    echo "CCACHE_BASEDIR=$GITHUB_WORKSPACE"
} >> "$GITHUB_ENV"
toolchain=$({ "$CC" --version; "$CXX" --version; ccache --version | head -1; echo "${ImageOS:-} ${ImageVersion:-}"; } | shasum -a 256 | cut -c1-16)
inputs=$(git ls-files -s Makefile third_party/archives.tsv tools/toolchain | shasum -a 256 | cut -c1-16)
echo "prefix=ccache-v3-$RUNNER_OS-$RUNNER_ARCH-$toolchain-$inputs" >> "$GITHUB_OUTPUT"
echo "compatibility-prefix=ccache-v3-$RUNNER_OS-$RUNNER_ARCH-$toolchain" >> "$GITHUB_OUTPUT"
