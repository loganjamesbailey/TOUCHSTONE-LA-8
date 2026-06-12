#!/bin/bash
# Host validation: refresh the AU registration cache, then auval and
# pluginval (strictness 10) on both installed formats.
set -euo pipefail
cd "$(dirname "$0")/.."

killall -9 AudioComponentRegistrar 2>/dev/null || true
sleep 1

echo "== auval =="
auval -v aufx Tstn Jbco

PV=tools/pluginval.app/Contents/MacOS/pluginval
[ -x "$PV" ] || { echo "run scripts/get_pluginval.sh first"; exit 1; }

echo "== pluginval VST3 =="
"$PV" --strictness-level 10 --timeout-ms 600000 \
      --validate "$HOME/Library/Audio/Plug-Ins/VST3/Touchstone.vst3"

echo "== pluginval AU =="
"$PV" --strictness-level 10 --timeout-ms 600000 \
      --validate "$HOME/Library/Audio/Plug-Ins/Components/Touchstone.component"

echo "ALL HOST VALIDATION PASSED"
