#!/bin/bash
# Run the offline verification harness. JSON to stdout (or $1), summary on
# stderr. Exit code 0 iff every guarantee in docs/SPECS.md holds.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="${1:-/dev/stdout}"
./build/tests/refcomp_tests > "$OUT"
