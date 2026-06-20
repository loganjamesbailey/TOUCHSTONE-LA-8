#!/bin/bash
# Touchstone build. Run from the repo root: ./scripts/build.sh
#
# If a future CMake 4.x refuses JUCE's bundled min-version declarations,
# add: -DCMAKE_POLICY_VERSION_MINIMUM=3.5
set -euo pipefail
cd "$(dirname "$0")/.."

cmake -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build -j"$(sysctl -n hw.ncpu)"
