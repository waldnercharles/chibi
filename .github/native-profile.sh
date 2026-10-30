#!/bin/sh
# CI's Debug profile, shared by every native job so their flags can't drift.
# Sanitizer runs keep frame pointers off; the build driver defaults are unchanged.
set -eu
debug='-g -O1 -fno-omit-frame-pointer' profile=debug pointers=on
if [ "$CHIBI_CI_SANITIZERS" = ON ]; then debug='-g -O1' profile=sanitizer pointers=; fi
{
    echo "BUILD_PROFILE=$profile"
    echo "BUILD_DEBUG_OPTIMIZATION=1"
    echo "BUILD_DEBUG_FRAME_POINTERS=$pointers"
    echo "SANITIZER_CMAKE_FLAGS=-DCMAKE_BUILD_TYPE=Debug -DCHIBI_ENABLE_SANITIZERS=$CHIBI_CI_SANITIZERS -DCHIBI_SANITIZERS=address,undefined -DCHIBI_ENABLE_WARNINGS=ON -DCHIBI_ENABLE_CCACHE=ON -DCMAKE_C_FLAGS_DEBUG=\"$debug\" -DCMAKE_CXX_FLAGS_DEBUG=\"$debug\" -DCMAKE_OBJC_FLAGS_DEBUG=\"$debug\""
} >> "$GITHUB_ENV"
