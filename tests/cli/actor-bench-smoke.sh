#!/usr/bin/env bash
#
# CLI check: every actor benchmark still runs and still verifies its own work.
# Run at a tiny size (PROTOSCALA_BENCH_N=2000) so a broken benchmark cannot rot
# unnoticed between full runs; timings are not checked here.
#
# Usage: actor-bench-smoke.sh <path-to-protoscala> <tests-source-dir>
set -u
P="${1:?usage: actor-bench-smoke.sh <protoscala> <tests-dir>}"
TESTS="${2:?usage: actor-bench-smoke.sh <protoscala> <tests-dir>}"
BENCH="$(cd "$TESTS/../benchmarks/actors" && pwd)"

rc=0
for f in "$BENCH"/*.scala; do
    out=$(PROTOSCALA_BENCH_N=2000 PROTOSCALA_ACTOR_WORKERS=2 timeout 120s "$P" "$f" 2>&1)
    last=$(printf '%s' "$out" | tail -n 1)
    if [[ "$last" != "ok" ]]; then
        echo "FAIL ($(basename "$f")): last line '$last'"
        rc=1
        continue
    fi
    if ! grep -qE '^mode=\S+ messages=[0-9]+ processed=-?[0-9]+$' <<<"$out"; then
        echo "FAIL ($(basename "$f")): no self-report line"
        rc=1
    fi
done
[[ $rc -eq 0 ]] && echo OK
exit $rc
