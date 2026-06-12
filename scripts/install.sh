#!/bin/sh
# Touchstone installer — fetches the latest release and installs the AU and
# VST3 plugins for the current user. No admin password needed.
#
# Files fetched with curl never receive macOS's quarantine flag, so the
# plugins load without Gatekeeper warnings — this is the supported free
# install path until releases are notarized.
#
# Usage (one line, from the website):
#   /bin/sh -c "$(curl -fsSL https://REPO_URL_HERE/raw/main/scripts/install.sh)"
set -eu

URL="https://REPO_URL_HERE/releases/latest/download/Touchstone-macOS.zip"

echo "Touchstone installer"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

echo "downloading..."
curl -fSL --progress-bar "$URL" -o "$TMP/touchstone.zip"
unzip -q "$TMP/touchstone.zip" -d "$TMP"

SRC=$(find "$TMP" -maxdepth 1 -type d -name "Touchstone-*" | head -1)
[ -n "$SRC" ] || { echo "unexpected archive layout"; exit 1; }

AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
mkdir -p "$AU_DIR" "$VST3_DIR"

rm -rf "$AU_DIR/Touchstone.component" "$VST3_DIR/Touchstone.vst3"
cp -R "$SRC/Touchstone.component" "$AU_DIR/"
cp -R "$SRC/Touchstone.vst3" "$VST3_DIR/"

# Belt and braces: clear quarantine in case the zip came via a browser.
xattr -dr com.apple.quarantine "$AU_DIR/Touchstone.component" 2>/dev/null || true
xattr -dr com.apple.quarantine "$VST3_DIR/Touchstone.vst3" 2>/dev/null || true

# Refresh the AU registration cache so Logic sees it without a reboot.
killall -9 AudioComponentRegistrar 2>/dev/null || true

echo ""
echo "Installed:"
echo "  $AU_DIR/Touchstone.component"
echo "  $VST3_DIR/Touchstone.vst3"
echo "Restart your DAW. In Logic Pro: Audio Units > JamesBailey > Touchstone."
