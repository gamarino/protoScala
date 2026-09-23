#!/usr/bin/env bash
# actor-bench.sh - thin wrapper around the protoScala actor benchmark harness.
#
# Runs the seven modes of docs/DESIGN.md §8.5 at six worker counts, verifies
# every run's self-reported message count before computing a rate, compares the
# four shapes protoClojure also has on the same machine and day, and writes
# benchmarks/reports/YYYY-MM-DD-<name>.md.
#
# Usage:
#   benchmarks/actor-bench.sh [--name NAME] [--workers 1,2,4,6,8,16]
#                             [--only single,MPSC] [--size N] [--no-compare]
#
# Honoured env vars: PROTOSCALA_BIN, PROTOCLJ_BIN, PROTOCLJ_BENCH.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec python3 "${SCRIPT_DIR}/run_actor_benchmarks.py" "$@"
