#!/usr/bin/env bash
#
# Cold-start measurement (DESIGN §1: < 20 ms to a prompt). Runs
#   (a) examples/hello.scala and
#   (b) a REPL session that quits immediately
# N times each, verifies every run's output, and reports the median wall
# time in milliseconds. Exits 1 when a run's output is wrong or a median is
# not below the target. Not part of ctest: timings depend on the machine
# and its load (see docs/plans Open question Q16).
#
# Usage: benchmarks/cold-start.sh [path-to-protoscala] [runs]
set -u
# Number formatting (awk printf "%.2f") and parsing must not follow the
# user's locale: a decimal comma would break the target comparison.
export LC_ALL=C
root=$(cd "$(dirname "$0")/.." && pwd)
P="${1:-$root/build_release/protoscala}"
N="${2:-21}"
TARGET_MS=20

median_ms() {  # reads nanosecond samples on stdin, prints the median in ms
    sort -n | awk '{ a[NR] = $1 } END { printf "%.2f", a[int((NR + 1) / 2)] / 1e6 }'
}

run_case() {  # $1 label, $2 expected last line, $3... command
    local label="$1" expected="$2"; shift 2
    local samples="" ok=0 i
    for ((i = 0; i < N; i++)); do
        local t0 t1 out
        t0=$(date +%s%N)
        out=$("$@" 2>&1 | awk 'NF{ last=$0 } END{ print last }')
        t1=$(date +%s%N)
        [[ "$out" == "$expected" ]] && ok=$((ok + 1))
        samples+="$((t1 - t0))"$'\n'
    done
    local med
    med=$(printf '%s' "$samples" | median_ms)
    echo "$label: runs=$N verified=$ok median_ms=$med target_ms=$TARGET_MS"
    [[ $ok -eq $N ]] || { echo "$label: FAIL: $((N - ok)) run(s) printed the wrong output"; return 1; }
    awk -v m="$med" -v t="$TARGET_MS" 'BEGIN { exit !(m < t) }' \
        || { echo "$label: FAIL: median ${med} ms is not below ${TARGET_MS} ms"; return 1; }
}

status=0
run_case "script" "Hello, protoScala!" "$P" "$root/examples/hello.scala" || status=1
run_case "repl" "scala> " sh -c "printf ':quit\n' | '$P'" || status=1
exit $status
