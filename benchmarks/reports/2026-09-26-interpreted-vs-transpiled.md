# The same source, interpreted and transpiled — 2026-09-26

**Question.** What does interpreting cost? One source, two execution paths, so the
delta isolates it.

**Answer, stated before the tables because the tables invite the wrong reading:**
removing the dispatch loop is worth **nothing measurable**, and making a function a
`proto::ProtoMethod` — which is the whole point of the phase — costs **2 to 2.6× on a
call-bound workload**. Interpretation is not where the cost lives. The calling
convention is.

Harness: `benchmarks/run_transpiled_benchmarks.py`. Raw JSON:
`.agent_scratch/phase7-transpiler/bench-transpiled.json`.

---

## 1. What this measures, and what it does not

Both paths call the **same** opcode bodies. `src/runtime/OpcodeOps.h` holds one
implementation of every opcode and both `ExecutionEngine::runLoop` and the C++
`protoscalac` emits call it — that is the architecture Phase 7 deliberately chose.
So this is **not** compiled-versus-interpreted arithmetic: `a + b` is the same
function call either way.

| removed by the transpiled path | **not** removed; identical on both |
|---|---|
| instruction fetch, operand decode, the `switch` | dynamic method dispatch through the prototype chain |
| the stack machine's push/pop, replaced by a constant slot index the emitter knew | attribute lookup |
| the per-frame bytecode setup | allocation and collection |
| the front end for the *user program* (parse, desugar, compile) | every prelude call |

**This is a diagnostic, not a performance claim.** protoScala's positioning is an
agile, interoperable, easily integrable and very simple Scala — explicitly not a fast
one. A small delta would have been the more valuable result. What was measured is
smaller than small in one direction and clearly negative in the other, and both
halves of that are reported.

## 2. Method

The host is **not quiet**: 2–5 of its 12 CPUs carry the maintainer's desktop and swap
is full. So the two paths are **interleaved round-robin inside one window**, adjacent
per workload, never in blocks. Contention then hits both columns equally and **the
ratio survives even though the absolutes do not.**

- 1 discarded warm-up round, then **5 timed rounds**; every cell is n = 5, and its
  min–max spread is printed.
- **Load average: 3.52 at the start, 3.90 at the midpoint, 5.06 at the end.**
- Every sample — warm-up included — is verified against the workload's own
  `// EXPECT:` line. **0 verification failures.** A crash that exits 0 is never a win.
- Each sample is one cold process, so start-up is in every figure on both sides.

## 3. The result

Absolutes are **contended** and are given only so the ratio can be checked; the ratio
is the finding.

| workload | interpreted (ms) | transpiled (ms) | **ratio** | interp [min–max] | trans [min–max] |
|---|---:|---:|---:|---|---|
| `fib30` (recursive, 2.69 M calls) | 472.3 | 1212.4 | **2.57** | 436–653 | 1110–1699 |
| `fib` (recursive) | 62.5 | 128.4 | **2.06** | 60–66 | 116–145 |
| `tak` (deep non-tail recursion) | 34.5 | 46.8 | **1.36** | 31–40 | 44–47 |
| `list_ops` (map / filter / foldLeft, 100 000) | 775.9 | 1050.5 | **1.35** | 583–918 | 802–1179 |
| `sum_loop` (0..1 000 000) | 86.7 | 90.5 | 1.04 | 74–96 | 86–96 |
| `int_sum_loop` (0 until 100 000) | 28.1 | 28.7 | 1.02 | 25–35 | 26–35 |
| `map_build` (50 000 entries) | 445.4 | 449.3 | 1.01 | 374–450 | 381–514 |
| `range_iterate` (100 000) | 31.3 | 30.6 | 0.98 | 26–34 | 26–39 |
| `str_concat` (2 000) | 26.7 | 26.1 | 0.98 | 22–43 | 21–32 |
| `factorial_100` | 24.6 | 21.9 | 0.89 | 21–31 | 20–26 |

**Geomean over the ten: 1.244. Median: 1.032.** Split by shape:

- **Call- and closure-bound (4 workloads): 1.35 – 2.57× SLOWER transpiled.** The
  ranges do not overlap for `fib`, `fib30` or `tak`, so this is not contention.
- **Everything else (6 workloads): geomean 0.985**, with heavily overlapping ranges.
  **Indistinguishable.**

**The null program is the cleanest number here.** A program whose only statement is
one `println` measures **22.29 ms interpreted and 22.40 ms transpiled**, n = 5. So
removing the user program's parse, desugar and compile is worth **nothing
measurable** — which is the expected result, and the reason is on the record: Phase
6's prelude image already removed the large share, and a one-file user program's
front end is small.

**That 22.3 ms is not the cold-start figure, and the difference is the point of the
cold-start correction.** `benchmarks/cold-start.sh` measures **26.38 ms** for a script
and the `< 25 ms` budget is **MISSED**; this runner measures 22.29 ms for a
near-identical program. The gap is the cold-start harness's own floor — about **5 ms**,
measured as 5.55 ms for `/bin/true` through the identical shell construct — which that
harness includes and this one does not, because this one calls `subprocess.run`
directly. Both figures are right for what they measure; neither may be quoted as the
other. Start-up is also **kernel-bound** (0.26 s user against 1.07 s sys across 42
runs), so a difference of a few milliseconds between two harnesses is a difference in
how many processes each one forks. The previously published 23.73 ms did not reproduce
even at a lower load.

Because that floor is ~22 ms of the ~25 ms several workloads take, a "work-only"
ratio derived by subtracting it is meaningless for the short ones — `factorial_100`
produces a **negative** work component. The derived column is therefore **not
published**; the whole-process ratio above is the figure, and the floor is stated
beside it as an operand.

## 4. Why the transpiled path is slower on a call

Not a mystery, and not contention: it is the calling convention, and it follows from
the phase's own design decision.

A `proto::ProtoMethod` is
`(ctx, self, ParentLink*, ProtoList* positional, ProtoSparseList* keywords)`. So
`ExecutionEngine::execute`'s native-entry branch must, **per call**:

1. open a `proto::ProtoContext` — and the generated body then opens its own
   `gen::Frame`, so a transpiled call costs **two** contexts where the interpreter
   costs one;
2. **allocate a `ProtoList`** for the arguments, because the convention takes a list
   where the interpreter passes a `const ProtoObject* const*` C array straight into
   the frame's slots;
3. enter `gen::enterMethod`'s `translateForeignException` region (§D6 site 2, which a
   native callable must present);
4. and the `Frame` then reads the arguments back **out** of that list.

`fib(30)` is 2.69 M calls, so that is 2.69 M extra contexts and 2.69 M list
allocations. The 2.57× is that, and it is paid for the capability the phase exists to
add: a transpiled function is callable by any runtime in the family precisely
*because* it presents that convention.

A second, smaller cost explains why the loops do not win either: in the interpreter
the opcode bodies are `inline` functions called from `runLoop` in the **same
translation unit**, so `gen::add` is inlined into the dispatch loop. In a generated
module every one of them is a **cross-DSO call into `libprotoScala.so`**. The
transpiled loop therefore trades an inlined body plus a `switch` arm for an
out-of-line call — and the two roughly cancel, which is exactly what `sum_loop` at
1.04 and `int_sum_loop` at 1.02 show.

## 5. What has no transpiled twin, and why

Listed rather than omitted: a comparison table with silent gaps is the same fault as
a wildcard exclusion.

| workload | why there is no transpiled twin |
|---|---|
| `attr_lookup` | **D118** — it declares a class, which `protoscalac` refuses |
| `object_tree` | **D118** — a case-class tree; the workload *is* classes |
| all 9 of `benchmarks/actors/*.scala` | **D113** — `await` is refused at transpile time, so there is nothing to compare. `actor-await` needs it directly; the rest declare case classes and are refused under D118 as well |

`attr_lookup` and `object_tree` are the two workloads DESIGN §1 names as the kind of
work protoScala is *for* — a deep object graph, attribute lookup, structural sharing.
**Neither can be transpiled today**, so this table measures the integer loops and the
recursion, which is the part of the suite protoScala is explicitly not optimised for.
That is a limit of the measurement and is stated as one.

## 6. What follows from this

1. **The dispatch loop is not where the cost is.** Removing it is worth nothing
   measurable on any workload here. Any future optimisation effort belongs in the
   **object model** — attribute lookup, allocation, the send path — not in the
   interpreter's `switch`. That is the useful outcome of this measurement, and it is
   the one the positioning already predicted.
2. **The transpiler must not be presented as a speed-up.** On the shape of program it
   accepts today it is 1.24× slower on a geometric mean and 2.57× slower at worst.
   The phase's own framing said it is not a performance feature; this is the number
   that makes that concrete rather than defensive.
3. **There is a cheap improvement available, and it is not in the emitter.** The
   per-call `ProtoList` and the second `ProtoContext` are `execute`'s native-entry
   branch, not generated code. A convention that let a native block receive the
   caller's argument array directly would remove both. Whether protoCore's
   `ProtoMethod` signature can grow such a path is a **protoCore** question (P3), and
   it is not asked here.
4. **Nothing about the object-model workloads is known.** `attr_lookup` and
   `object_tree` are refused, so the workloads that would actually exercise
   protoScala's purpose are unmeasured on the transpiled path until D118 lands.
