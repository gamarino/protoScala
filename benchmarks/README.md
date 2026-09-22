# Benchmarks

Arrive with Phase 3 (`bench.sh`: fib, tak, sum-loop, list-ops, map-build) and
Phase 5 (`actor-bench.sh`: MPSC, fan-out, ping-pong). Every benchmark prints
the result it computed and the runner verifies it — an exit code alone never
counts as success. Results are recorded in `RESULTS.md` with machine, date and
commit.
