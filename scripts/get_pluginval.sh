#!/bin/bash
# Download pluginval into tools/ and strip the quarantine attribute.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p tools
curl -sL -o tools/pluginval.zip \
    "https://github.com/Tracktion/pluginval/releases/latest/download/pluginval_macOS.zip"
(cd tools && unzip -o -q pluginval.zip)
xattr -dr com.apple.quarantine tools/pluginval.app 2>/dev/null || true
echo "pluginval ready: tools/pluginval.app"
