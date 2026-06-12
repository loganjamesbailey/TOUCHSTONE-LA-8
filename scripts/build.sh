#!/bin/bash
# Touchstone build. Run from the repo root: ./scripts/build.sh
#
# NOTE on the CXXFLAGS below: this machine's Command Line Tools install is
# missing most of its bundled libc++ headers (/Library/Developer/
# CommandLineTools/usr/include/c++/v1 has 11 stray files from a 2022
# install that SHADOW the complete SDK copy). Until CLT is reinstalled,
# every compile needs -nostdinc++ plus the SDK's libc++ headers.
# If a future CMake 4.x refuses JUCE's bundled min-version declarations,
# add: -DCMAKE_POLICY_VERSION_MINIMUM=3.5
set -euo pipefail
cd "$(dirname "$0")/.."

SDK=/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk
export SDKROOT="$SDK"
export CXXFLAGS="-nostdinc++ -isystem $SDK/usr/include/c++/v1"
export OBJCXXFLAGS="-nostdinc++ -isystem $SDK/usr/include/c++/v1"

cmake -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build -j"$(sysctl -n hw.ncpu)"
