# Benchmarks

Arrive with Phase 3 (`bench.sh`: fib, tak, sum-loop, list-ops, map-build) and
Phase 5 (`actor-bench.sh`, modelled on protoClojure's: single, fan-out, MPSC,
MPMC, ping-pong, await, priority — see docs/DESIGN.md §8.5). Every benchmark prints
the result it computed and the runner verifies it — an exit code alone never
counts as success. Results are recorded in `RESULTS.md` with machine, date and
commit.
