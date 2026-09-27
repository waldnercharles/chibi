#!/bin/sh
# Prepare a fresh Ubuntu container (Claude Code on the web) for `make test`
# and `make test-render`: CI's packages, the ASan runtime, CMake 4.2+ and the
# clang-format major version CI uses. Safe to run again.
set -eu

sudo=
[ "$(id -u)" -eq 0 ] || sudo=sudo
root=$(cd "$(dirname "$0")/.." && pwd -P)
clang_format=$(sed -n 's/^CLANG_FORMAT_VERSION=//p' "$root/tools/clang-format.sh")

# Unreachable third-party PPAs must not stop the Ubuntu archive from updating.
$sudo apt-get update || :
DEBIAN_FRONTEND=noninteractive $sudo apt-get install -y \
	clang-18 libclang-18-dev libclang-rt-18-dev ninja-build ccache \
	libgl1-mesa-dev libvulkan1 mesa-vulkan-drivers \
	libx11-dev libxcursor-dev libxext-dev libxi-dev libxrandr-dev libxss-dev \
	libxtst-dev libwayland-dev libxkbcommon-dev xvfb
python3 -m pip install --quiet 'cmake>=4.2' "clang-format==$clang_format.*"

# CMake takes cc and c++ at first configure; the project builds with Clang.
$sudo update-alternatives --install /usr/bin/cc cc /usr/bin/clang-18 100
$sudo update-alternatives --install /usr/bin/c++ c++ /usr/bin/clang++-18 100
