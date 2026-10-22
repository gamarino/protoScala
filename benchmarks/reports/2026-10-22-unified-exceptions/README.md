# protoScala on protoCore's unified exception model: final measurement (2026-10-22)

The interpreter, the transpiled path (protoscalac), and the compiler itself.

## Builds

| | protoScala | protoCore | Notes |
|---|---|---|---|
| **BEFORE** | `3783caa8` | `cf52f9f5` | 3.0.0 calling convention, no `ProtoException` |
| **AFTER** | `1e1cf911` (`feature/protocore-3-calling-convention` tip) | `65760e4a` (`feature/args-calling-convention` tip) | `110be0ac` port, `54d901ac`, D134-D136 |

- **Worktrees**: clean, detached, under
  `.agent_scratch/meas_1022/{before,after}/{protoCore,protoScala,protoIO}`.
  - protoIO `1611ff9` is used on both sides.
- **Build**: Release, with `-DCMAKE_DISABLE_FIND_PACKAGE_protoCore=ON` (the
  developer fallback to the sibling `../protoCore/build_release`) and
  `-DPROTOSCALA_PROTOST_INTEROP=OFF`.
  - The interop test is a test executable only, and is not measured.
  - The targets built were `protoscala`, `protoscalac` and `libprotoScala`.
  - `build3.sh` built them one at a time, at `-j4`, before any measurement.
- **Library loading**
  - Every run loaded a fresh copy of its variant's `libprotoCore.so.4`
    through `LD_LIBRARY_PATH` (`ldd.txt`, `ldd_verify.txt`).
  - Every transpiled `module.so` resolved its own variant's `libprotoScala.so.1`
    and the fresh `libprotoCore` (`verify.txt`).
  - `libprotoScala.so.1` itself was *not* freshly copied per run.
- **Quiet gate**: **`QUIET_CORES=4.0`** (`quiet_*.txt`).
  - 1.8-3.9 CPUs were busy at the gates (VS Code GPU, Chrome, PyCharm), and
    the load average was 2.8-6.3.
  - The harness's own load-average wait was disabled with `--max-load 100`;
    the quiet gate replaces it.
  - **Only the interleaved BEFORE/AFTER ratios are meaningful.**
- **Self-report check** (`sc_verify.sh` → `verify.txt`)
  - Coverage: all 13 comparables and the 6 probes, each interpreted and
    transpiled, BEFORE and AFTER, checked against the file's `// EXPECT:` line.
    The probes were also run on the JVM (scalac 3.9.0, OpenJDK 25).
  - Result: everything OK, except the 2 expected `NOSO` lines. `case_copy` has
    no transpiled twin on either side: D121, named arguments are not supported
    by protoscalac.
  - Every harness sample is also verified by the harness itself. All 6
    interpreter runs ended with rc=0 and no `FAILED` cell. All 6 transpiled
    runs reported `failures: []`.

## 1. Interpreter: `benchmarks/run_benchmarks.py`

- **Method**
  - Three rounds, with the variant order alternated.
  - Each harness run does 2 warm-ups and 5 timed runs per cell, round-robin
    with the JVM and CPython.
  - The figures are the medians of the three per-round medians, in ms per cold
    process.
- **Comparison runtimes**
  - Scala 3.9.0 on OpenJDK 25: the same `.scala` file.
  - CPython 3.14.0: the harness's twins.
  - The protopy, protost and protoclj columns were switched off. Those
    runtimes are measured in their own reports.

| Workload | Scala JVM | CPython | BEFORE | AFTER | A/B | BEFORE rounds | AFTER rounds |
|---|---:|---:|---:|---:|---:|---|---|
| `int_sum_loop` | 219.1 | 45.4 | 35.7 | 35.5 | 0.994 | 36/36/34 | 36/36/35 |
| `fib` | 216.2 | 54.1 | 60.7 | 60.0 | 0.988 | 61/61/59 | 61/58/60 |
| `str_concat` | 228.4 | 39.9 | 31.9 | 31.6 | 0.991 | 34/32/30 | 31/32/32 |
| `range_iterate` | 235.8 | 46.9 | 38.0 | 36.3 | 0.955 | 41/38/35 | 35/36/39 |
| `tak` | 214.8 | 42.7 | 45.3 | 37.9 | 0.837 | 45/51/38 | 41/38/37 |
| `fib30` | 220.1 | 179.5 | 398.9 | 384.0 | 0.963 | 399/515/385 | 385/378/384 |
| `sum_loop` | 223.6 | 99.8 | 85.9 | 86.0 | 1.001 | 86/99/85 | 84/86/102 |
| `factorial_100` | 224.2 | 40.5 | 32.7 | 29.7 | 0.908 | 31/33/33 | 29/30/36 |
| `attr_lookup` | 222.9 | 53.2 | 50.7 | 51.9 | 1.024 | 51/58/49 | 52/52/54 |
| `object_tree` | 246.4 | 211.6 | 342.2 | 331.7 | 0.969 | 323/376/342 | 332/321/364 |
| `list_ops` | 294.4 | 1953.4 | 507.7 | 480.5 | 0.946 | 486/508/508 | 464/480/518 |
| `map_build` | 393.4 | 88.2 | 312.6 | 303.4 | 0.971 | 297/313/326 | 303/302/421 |
| `case_copy` | 272.6 | 1488.2 | 748.9 | 769.3 | 1.027 | 749/746/909 | 736/769/838 |

| | Geomean (13) |
|---|---:|
| AFTER / BEFORE | **0.966** |
| BEFORE / Scala JVM | 0.44x |
| AFTER / Scala JVM | 0.43x |
| BEFORE / CPython | 0.97x |
| AFTER / CPython | 0.94x |

- Each sample is a cold process, so the JVM pays its start-up of about 200 ms
  in every cell. That is the harness's design.
- Cold start (`cold-start.sh`, 21 runs):
  - script: 35.4 → 35.9 ms
  - REPL: 36.7 → 36.1 ms
  - Both miss the 25 ms target, as before. The machine is contended.
- **The interpreter is neutral to slightly faster.**
  - The largest gain, `tak` (−16 %), has overlapping rounds (45/51/38 against
    41/38/37). It is not claimed.
  - The worst cells, `case_copy` (+2.7 %) and `attr_lookup` (+2.4 %), lie within
    their round spread.

## 2. Transpiled vs interpreted: `benchmarks/run_transpiled_benchmarks.py`

- **Method**
  - The runner's default: 1 warm-up round and 5 timed rounds, interpreted and
    transpiled round-robin.
  - Each variant ran its own runner; the file is identical in both commits.
  - Three runs per variant, with the order alternated. The figures are the
    medians of the per-run medians, in ms.
- **How the runner was run.** The user ruled that nothing may be deleted during
  this task, and the runner (like `run_benchmarks.py`'s `build_jvm/`) clears
  its output directory. So both scripts were run from copies in
  [`harness_copies/`](harness_copies/), changed in exactly two lines:
  - the source path is fixed (`SCRIPT_DIR` / `HERE`);
  - the output directory comes from the environment (`JVM_BUILD_DIR` /
    `SCRATCH`), and is a new, empty directory for every run.

| Workload | interp BEFORE | interp AFTER | interp A/B | trans BEFORE | trans AFTER | trans A/B | trans/interp BEFORE | trans/interp AFTER |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `attr_lookup` | 55.0 | 52.7 | 0.958 | 54.8 | 49.5 | 0.903 | 1.00 | 0.94 |
| `case_copy` | 846.1 | 807.1 | 0.954 | — (D121) | — | — | — | — |
| `factorial_100` | 33.8 | 49.9 | 1.474 | 29.9 | 30.8 | 1.032 | 0.88 | 0.62 |
| `fib` | 65.9 | 72.2 | 1.096 | 85.5 | 87.3 | 1.021 | 1.30 | 1.21 |
| `fib30` | 418.5 | 394.9 | 0.944 | 635.8 | 640.7 | 1.008 | 1.52 | 1.62 |
| `int_sum_loop` | 43.6 | 38.4 | 0.883 | 48.2 | 44.7 | 0.927 | 1.11 | 1.16 |
| `list_ops` | 636.8 | 625.2 | 0.982 | 666.9 | 673.1 | 1.009 | 1.05 | 1.08 |
| `map_build` | 344.5 | 343.1 | 0.996 | 353.5 | 341.2 | 0.965 | 1.03 | 0.99 |
| `object_tree` | 370.1 | 363.7 | 0.983 | 478.0 | 472.8 | 0.989 | 1.29 | 1.30 |
| `range_iterate` | 50.8 | 43.6 | 0.858 | 38.8 | 38.4 | 0.990 | 0.76 | 0.88 |
| `str_concat` | 31.8 | 33.2 | 1.042 | 32.2 | 30.7 | 0.953 | 1.01 | 0.92 |
| `sum_loop` | 96.1 | 95.2 | 0.991 | 114.5 | 104.7 | 0.914 | 1.19 | 1.10 |
| `tak` | 48.1 | 48.1 | 1.000 | 44.5 | 46.5 | 1.044 | 0.93 | 0.97 |
| null program | 36.8 | 29.4 | 0.800 | 33.7 | 30.0 | 0.890 | | |

| Geomean | Value |
|---|---:|
| interpreted A/B (13) | 1.003 |
| **transpiled A/B (12)** | **0.979** |
| transpiled / interpreted, BEFORE | 1.07 |
| transpiled / interpreted, AFTER | 1.04 |

- **The transpiled path does not regress on the normal path** (0.979).
- **This runner is noisier than the interpreter harness.**
  - Its interpreter column disagrees with section 1 on `factorial_100`:
    1.47x here, 0.91x there. The per-run medians were 34/35/34 against
    35/52/50 ms.
  - Use section 1 for the interpreter A/B, and this table for the trans/interp
    ratio and the transpiled A/B.
- **Transpiled is *slower* than interpreted on the call-heavy workloads, on both
  sides**: `fib30` 1.5-1.6x, `fib` 1.2-1.3x, `object_tree` 1.3x. The geomean is
  1.04-1.07x.
  - The port does not cause this.
  - It contradicts the expectation that removing the dispatch loop helps. It is
    worth an investigation of its own; it was not investigated here.
- **JVM**: the transpiled path is not compared with the JVM here. See section 1
  for interpreted vs JVM. The cold-process JVM cells are about 220 ms almost
  everywhere, against 30-670 ms transpiled.

## 3. Compile time: `.scala` → loadable `module.so`

- **Method**
  - Wall time of `protoscalac X.scala -o DIR --build-so`, BEFORE and AFTER
    interleaved.
  - The main pass made 3 repetitions (`compile.txt`). A second pass made 5
    more (`recheck/compile.txt`).

| Source | BEFORE ms | AFTER ms | A/B (3 reps) | A/B (5 reps) | C++ lines B → A | `module.so` bytes B → A |
|---|---:|---:|---:|---:|---:|---:|
| `fib` | 858 | 849 | 0.990 | 1.043 | 241 → 241 | 24,888 → 24,888 |
| `tak` | 917 | 887 | 0.967 | — | 257 → 257 | 24,888 → 24,888 |
| `object_tree` | 1011 | 1129 | 1.117 | 1.025 | 1223 → 1223 | 66,352 → 66,352 |
| `list_ops` | 913 | 886 | 0.971 | — | 455 → 455 | 32,144 → 32,144 |
| `throw_catch` (probe) | 887 | 969 | 1.092 | 1.056 | 308 → 313 | 26,288 → 30,776 |
| `throw_across5` (probe) | 898 | 960 | 1.068 | 1.070 | 390 → 395 | 39,656 → 39,936 |
| `zerodiv` (probe) | 867 | 946 | 1.091 | 1.104 | 310 → 315 | 26,176 → 30,552 |
| `case_copy` | — | — | refused (D121) on both sides | | | |

- **Code without `try`**: identical C++, identical `module.so`, and compile time
  within noise (0.97-1.04).
- **Code with `try`/`catch`**
  - The emitter writes 5 more lines per handler.
  - `module.so` grows by 4.5 KB (+17 %) for a single `try`.
  - Compile time grows by **5-10 %, reproduced in both passes**.
  - This is the new `inflight` / `gen::rethrowInFrame` handler code. The C++
    compiler dominates the time.

## 4. Exception probes ([`probes/`](probes/), `probes.txt`, `recheck/probes.txt`)

- **Method**
  - `perf stat -r 3` cycles; all five variants interleaved per probe.
  - Each figure is cycles per iteration net of start-up: `startup.scala`'s
    cycles for the same variant are subtracted. Each probe's `CHECK` line is its
    count, and it was verified.
  - The transpiled probes ran `protoscala --run-module module.so`.
  - The JVM is a cold `java` process. Its start-up, about 1.3 G cycles, varies
    more than the loops cost, so the cheap probes come out "<0".
- **The probes**
  - `throw_across5` throws from `deep(0)`, under 5 non-tail frames of
    `1 + deep(d - 1)`.
  - `zerodiv` is `1 / z` with `z: Int = 0`, caught as `ArithmeticException`.
- **The BEFORE commit ran every probe**, interpreted and transpiled.
- Probes that moved by more than 5 % were run a second time, AFTER first
  (`recheck/probes.txt`).

| Probe | iters | interp B | interp A | interp A/B (2nd) | trans B | trans A | trans A/B (2nd) | JVM |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `empty_loop` | 2M | 197 | 193 | 0.98x | 274 | 253 | 0.92x | <0 (noise) |
| `try_no_throw` | 2M | 217 | 206 | 0.95x | 272 | 265 | 0.97x | <0 (noise) |
| `throw_catch` | 200k | 26,980 | 26,314 | 0.98x (1.12x) | 26,461 | 27,587 | 1.04x (1.01x) | 3,024 |
| `throw_across5` | 100k | 204,696 | 196,899 | 0.96x (0.98x) | 147,307 | 163,351 | **1.11x (1.11x)** | 4,215 |
| `zerodiv` | 200k | 37,456 | 40,079 | **1.07x (1.05x)** | 40,108 | 42,408 | **1.06x (1.14x)** | 3,397 |

- **Transpiled `throw_across5` is 11 % slower, reproduced exactly in the second
  pass.**
  - That is about +16,000 cycles per exception unwound through 5 transpiled
    frames.
  - It is consistent with the new per-frame handler code (`inflight` and
    `gen::rethrowInFrame`) that the emitter now places in each frame.
  - **A regression of the transpiled exception path.**
- **`zerodiv` is 5-14 % slower on both paths, in both passes.** The native
  `ArithmeticException` now goes through the unified model. **A regression.**
- Interpreted `throw_catch` read 0.98x, then 1.12x. **Noise-level; undecided.**
- A `try` that does not throw costs nothing measurable over the loop, on both
  sides and on both paths.
- **Against the JVM**
  - The JVM's warmed-up exceptions cost 3-4k cycles.
  - protoScala pays 26k for a same-frame catch: 9x.
  - Across 5 frames it pays 150-200k: 35-50x.
  - The transpiled path unwinds 5 frames 28 % cheaper than the interpreter
    BEFORE, but only 17 % cheaper AFTER. It is no cheaper on same-frame
    throws.

## Deviations and incidents

- **A first attempt was aborted.**
  - The user ruled that nothing may be deleted. The stock
    `run_benchmarks.py` `rmtree`s its `build_jvm/<workload>` directories before
    each compile, and the transpiled runner clears its scratch directory.
  - The first attempt (`.agent_scratch/meas_1022/sc_out/`) was killed during
    its second interpreter run. By then that run had already removed the
    `build_jvm/` class directories the first run created in the AFTER worktree.
    Those were scratch outputs of this task only.
  - Everything reported here comes from the second attempt (`sc_out_2/`), with
    the harness copies described in section 2. The first attempt's one
    complete run is kept in `aborted_first_attempt/` and is not used.
- **Earlier deletions, before the no-delete rule arrived**
  - The first configure failed: protoIO was missing.
  - The agent then ran `rm -rf` on its own freshly created, failed
    `build_release/` directories in the new worktrees.
  - `sc_verify.sh` and the first `sc_run.sh` contain `rm -rf` of their own
    output directories. They acted on directories created by this task only.
- `QUIET_CORES=4.0` instead of 2.5. `--max-load 100`.
- `libprotoScala.so.1` was not freshly copied per run. Only `libprotoCore` was.

## Files

- **Scripts**
  - `build3.sh`
  - `sc_verify.sh`
  - `sc_run_2.sh`: the measurement used. `sc_run.sh` is the aborted first
    version.
  - `recheck.sh`
  - `sc_analyze.py`, `sc_compile_recheck.py`, `uex_common.py`
  - `harness_copies/`
- **Raw data**
  - `interp_*_r*.{md,log}`, `trans_*_r*.{json,err}`
  - `compile.txt`, `probes.txt`, `recheck/`
  - `verify.txt`, `ldd*.txt`, `quiet_*.txt`
  - `analysis.txt`
- **Worktrees**, kept:
  `/home/gamarino/Documentos/proyectos/.agent_scratch/meas_1022/{before,after}/protoScala`,
  with builds in `build_release/`.
- **Generated modules**: `.agent_scratch/meas_1022/sc_out_2/{probe_so,compile,trans_scratch_*}`.
