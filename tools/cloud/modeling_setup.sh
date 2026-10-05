#!/bin/bash
# Optional Debian trixie modeling prerequisites; never changes host packages.
set -euo pipefail
root=${CHIBI_CLOUD_TOOLS:-/workspace/.cloud-tools}
[[ $root == /* ]] || { echo 'CHIBI_CLOUD_TOOLS must be an absolute path.' >&2; exit 1; }
[[ ! $root =~ [[:space:]] ]] || { echo 'CHIBI_CLOUD_TOOLS cannot contain whitespace: Make toolchain flags require a single path.' >&2; exit 1; }
script_dir=$(cd "$(dirname "$0")" && pwd -P)
version=4.3.2+dfsg-2

fail() {
    echo "modeling prerequisites: $*" >&2
    echo "Run tools/cloud/setup.sh modeling, then tools/cloud/modeling_setup.sh --check." >&2
    exit 1
}

check() {
    [ -x "$root/bin/blender" ] && [ -x "$root/root/usr/bin/blender" ] || fail "local Blender is missing"
    [ -f "$root/modeling-version" ] && [ "$(cat "$root/modeling-version")" = "$version" ] || fail "expected Debian blender and blender-data $version"
    [ -f "$root/root/usr/share/blender/scripts/modules/bpy/__init__.py" ] || fail "blender-data Python scripts are missing"
    [ -f "$root/root/usr/share/blender/datafiles/colormanagement/config.ocio" ] || fail "blender-data color management is missing"
    timeout 30s "$root/bin/blender" --background --factory-startup --threads 1 \
        --python-exit-code 1 --python "$script_dir/blender_smoke.py" || fail "bounded bpy/OBJ check failed"
    echo "blender=$version blender-data=$version (workspace-local; host dependencies)"
}

case ${1:-} in
    --check) check; exit ;;
    '') ;;
    *) echo "usage: $0 [--check]" >&2; exit 2 ;;
esac
[ $# -le 1 ] || fail "unexpected arguments"
[ -f "$root/apt.conf" ] || fail "cloud apt configuration is missing"
# Pinned official packages, including packages already installed on the host.
# APT resolves missing dependencies against the host Debian package database.
# The parent setup prepares verified Debian archive sources and package lists.
archives=$(mktemp -d "$root/apt/modeling-transaction.XXXXXX")
trap 'rm -rf "$archives"' EXIT
mkdir -p "$archives/partial" "$root/root" "$root/bin"
export APT_CONFIG="$root/apt.conf"
apt-get --download-only --reinstall --no-install-recommends -y \
    -o Debug::NoLocking=1 -o "Dir::Cache::archives=$archives" install \
    "blender=$version" "blender-data=$version"
for package in "$archives"/*.deb; do
    dpkg-deb -x "$package" "$root/root"
done
# Resolve the prefix from this wrapper so custom roots need no shell escaping.
cat > "$root/bin/blender" <<'WRAPPER'
#!/bin/bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd -P)
export BLENDER_SYSTEM_SCRIPTS="$root/root/usr/share/blender/scripts"
export BLENDER_SYSTEM_DATAFILES="$root/root/usr/share/blender/datafiles"
export LD_LIBRARY_PATH="$root/root/usr/lib/$(dpkg-architecture -qDEB_HOST_MULTIARCH)${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$root/root/usr/bin/blender" "$@"
WRAPPER
chmod +x "$root/bin/blender"
printf '%s\n' "$version" > "$root/modeling-version"
check
