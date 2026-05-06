#!/usr/bin/env bash
# Phase 0.5 determinism runner (POSIX side: macOS, Linux).
#
# Builds the standalone determinism_test binary and runs the named
# scenario.  CI then diffs the trace against the committed golden.
#
# Usage:
#   run_determinism.sh <scenario> <ticks> <out_trace>

set -euo pipefail

scenario=${1:-surface}
ticks=${2:-10000}
out=${3:-/tmp/defcon-determinism.trace}

case "$scenario" in
    surface|mirv|radar) ;;
    *) echo "unknown scenario: $scenario" >&2; exit 2 ;;
esac

# Resolve repo root from this script's location.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MAKE_DIR="$SCRIPT_DIR/determinism_make"

if [ ! -x "$MAKE_DIR/determinism_test" ]; then
    (cd "$MAKE_DIR" && make >/dev/null)
fi

"$MAKE_DIR/determinism_test" "$scenario" "$ticks" "$out"

if [ ! -s "$out" ]; then
    echo "no trace produced at $out" >&2
    exit 4
fi

echo "wrote $(wc -l <"$out") trace lines to $out"
