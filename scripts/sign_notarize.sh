#!/bin/bash
# Code-sign and notarize the packaged release. Prerequisites (one-time):
#   1. Apple Developer Program membership (developer.apple.com/programs)
#   2. A "Developer ID Application" certificate in your login keychain
#      (Xcode > Settings > Accounts > Manage Certificates, or
#       developer.apple.com/account/resources/certificates)
#   3. A notarytool keychain profile:
#      xcrun notarytool store-credentials touchstone-notary \
#          --apple-id YOUR_APPLE_ID --team-id YOUR_TEAM_ID \
#          --password APP_SPECIFIC_PASSWORD
#      (app-specific password from appleid.apple.com > Sign-In & Security)
#
# Usage: ./scripts/sign_notarize.sh "Developer ID Application: Your Name (TEAMID)"
set -euo pipefail
cd "$(dirname "$0")/.."

IDENTITY="${1:?usage: sign_notarize.sh \"Developer ID Application: Name (TEAMID)\"}"
PROFILE="${2:-touchstone-notary}"
VERSION=$(grep -oE 'project\(Touchstone VERSION [0-9.]+' CMakeLists.txt | grep -oE '[0-9.]+$')
STAGE="dist/Touchstone-$VERSION"
ZIP="dist/Touchstone-$VERSION-macOS.zip"

[ -d "$STAGE" ] || { echo "run scripts/package.sh first"; exit 1; }

for bundle in "$STAGE/Touchstone.component" "$STAGE/Touchstone.vst3"; do
    codesign --force --deep --options runtime --timestamp \
             --sign "$IDENTITY" "$bundle"
    codesign --verify --deep --strict "$bundle"
done

rm -f "$ZIP"
(cd dist && zip -r -q "Touchstone-$VERSION-macOS.zip" "Touchstone-$VERSION")

xcrun notarytool submit "$ZIP" --keychain-profile "$PROFILE" --wait

# Staple the tickets so installs work offline, then rebuild the final zip.
xcrun stapler staple "$STAGE/Touchstone.component"
xcrun stapler staple "$STAGE/Touchstone.vst3"
rm -f "$ZIP"
(cd dist && zip -r -q "Touchstone-$VERSION-macOS.zip" "Touchstone-$VERSION")

echo "SIGNED + NOTARIZED: $ZIP"
