#!/usr/bin/env bash
# Phase 0 determinism runner (POSIX side: macOS, Linux).
#
# Runs the DEFCON binary in headless determinism mode for one of the
# three pinned scenarios and writes the per-tick state-hash trace to
# the given output path.  CI then diffs the trace against the
# committed phase{0,1,2}.trace golden file.
#
# Phase 0 caveat: full headless mode (no SDL window, no audio) is not
# yet implemented; this script documents the contract.  Until headless
# mode lands, CI runs against the committed golden traces only as a
# build-and-link check.
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

DEFCON_BIN=${DEFCON_BIN:-./Defcon}
if [ ! -x "$DEFCON_BIN" ]; then
    echo "DEFCON binary not found at $DEFCON_BIN" >&2
    exit 3
fi

export DEFCON_DETERMINISM_TRACE="$out"
export DEFCON_DETERMINISM_SCENARIO="$scenario"
export DEFCON_DETERMINISM_TICKS="$ticks"

"$DEFCON_BIN"

if [ ! -s "$out" ]; then
    echo "no trace produced at $out" >&2
    exit 4
fi

echo "wrote $(wc -l <"$out") trace lines to $out"
