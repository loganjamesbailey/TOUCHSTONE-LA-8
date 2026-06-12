#!/bin/bash
# Universal (arm64 + x86_64) release build and distribution zip.
# Output: dist/Touchstone-<version>-macOS.zip
# Run scripts/sign_notarize.sh afterwards (needs Developer ID cert).
set -euo pipefail
cd "$(dirname "$0")/.."

SDK=/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk
export SDKROOT="$SDK"
export CXXFLAGS="-nostdinc++ -isystem $SDK/usr/include/c++/v1"
export OBJCXXFLAGS="-nostdinc++ -isystem $SDK/usr/include/c++/v1"

VERSION=$(grep -oE 'project\(Touchstone VERSION [0-9.]+' CMakeLists.txt | grep -oE '[0-9.]+$')
echo "Packaging Touchstone $VERSION (universal)"

cmake -B build-release -G "Unix Makefiles" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" > /dev/null
cmake --build build-release --target Touchstone_AU Touchstone_VST3 -j"$(sysctl -n hw.ncpu)"

AU="build-release/plugin/Touchstone_artefacts/Release/AU/Touchstone.component"
VST3="build-release/plugin/Touchstone_artefacts/Release/VST3/Touchstone.vst3"
lipo -archs "$AU/Contents/MacOS/Touchstone"

STAGE="dist/Touchstone-$VERSION"
rm -rf "$STAGE" && mkdir -p "$STAGE"
cp -R "$AU" "$VST3" "$STAGE/"
cp LICENSE "$STAGE/"
cat > "$STAGE/INSTALL.txt" <<EOF
Touchstone $VERSION — macOS (Apple Silicon + Intel)

Install:
  Copy Touchstone.component to  ~/Library/Audio/Plug-Ins/Components/
  Copy Touchstone.vst3      to  ~/Library/Audio/Plug-Ins/VST3/
Then restart your DAW. In Logic Pro, the plugin appears under
Audio Units > JamesBailey > Touchstone.

Requires macOS 11+. Source code and measurements:
see the project repository (AGPL-3.0).
EOF

(cd dist && rm -f "Touchstone-$VERSION-macOS.zip" \
  && zip -r -q "Touchstone-$VERSION-macOS.zip" "Touchstone-$VERSION")
echo "dist/Touchstone-$VERSION-macOS.zip ready (unsigned — run sign_notarize.sh)"
