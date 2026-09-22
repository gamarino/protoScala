#!/usr/bin/env bash
# bench.sh - thin wrapper around the protoScala benchmark harness.
#
# Runs the comparable workloads (benchmarks/comparable/*.scala) on protoScala
# and their twins on every runtime found (Scala on the JVM when SCALA_HOME is
# set, CPython, protopy, protost, protoclj), verifies every run's printed
# result, runs cold-start.sh, and writes benchmarks/reports/YYYY-MM-DD-<name>.md.
#
# Usage:
#   benchmarks/bench.sh [--name NAME] [--runs N] [--warmup N] [--only a,b]
#
# Honoured env vars: PROTOSCALA_BIN, PROTOSCALA_RELEASE_BIN, SCALA_HOME,
# JAVA_BIN, CPYTHON_BIN, PROTOPY_BIN, PROTOST_BIN, PROTOCLJ_BIN
# (see run_benchmarks.py).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec python3 "${SCRIPT_DIR}/run_benchmarks.py" "$@"
