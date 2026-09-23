# protoScala — task tracker

## Phase 0 — Skeleton (2026-09-22)
- [x] Design spec, language reference, roadmap, status, interop docs
- [x] Platform spec `ProtoMap`
- [x] CMake build against protoCore; `--version`, `--help`
- [x] Unit / conformance / CLI harnesses; hello-world XFAIL
- [x] Plans for Phase P1 and Phase 1
- [x] GitHub repository gamarino/protoScala (public, MIT)

## Phase 1 — Core language, 0.1.0 (2026-09-22)
- [x] Tasks 1–12: lexer, layout, parser, desugar, compiler, VM, primitives,
      script runner, REPL, tutorial chapters 1–5 and 14
- [x] Task 13: `examples/hello.scala`, `examples/fib.scala`, `cli/examples`,
      `benchmarks/cold-start.sh` + `RESULTS.md`, STATUS.md (D9–D25, opcode
      table mirror), ROADMAP.md, LANGUAGE.md (`this` keyword), tutorial D-id
      references, CHANGELOG.md `[0.1.0]`, README.md, `CMakeLists.txt` 0.1.0

## Next
- [ ] Phase P1 — Task 0: maintainer decisions (iterator option, helper location)
- [ ] Phase P2 — plan + Task 0: GC strategy for `ProtoMPSCQueue` with the maintainer
- [ ] Phase 2 — object model, `apply`, `for`, `match`

## Review
Phase 0: build and 6/6 tests verified on 2026-09-22 (2 unit, 3 CLI, 1
conformance XFAIL); the conformance runner was checked to fail on a
non-conforming EXPECT and to pass a matching EXPECT-ERROR.

Phase 1 (0.1.0): 307/307 tests green (199 unit, 94 conformance, 14 CLI) on
2026-09-22; `protoscala --version` prints `protoScala 0.1.0`; cold start
measured with `benchmarks/cold-start.sh` — see `benchmarks/RESULTS.md`
(Release build: script 17.6–18.6 ms, REPL 19.1–20.4 ms median across
21-run passes, one borderline REPL sample over the 20 ms target on a loaded
machine; the default RelWithDebInfo build measured slower and less
consistently). D9–D25 recorded in STATUS.md as provisional, pending
maintainer review.

## Closing sequence (maintainer, 2026-09-22) — after Phase 2 and the protoCore fixes
- [ ] Merge protoCore feature/pslo-p1 (ProtoMap) then feature/descendant-of (isInstanceOf/hasParent/setParents) into master; version 2.0.0, SOVERSION 2
- [ ] protoCore: full suite, ASan, perf gate on a quiet host
- [ ] Clean rebuild + suites + fixes: protoPython, protoJS (-j1, sequential test262), protoST, protoClojure, protoScala
- [ ] Re-measure the whole benchmark suite and cold start against protoCore 2.0.0 on a QUIET machine
      (ruling: 0.2.0's published numbers move to the stack that ships; the e43fa2e4 run stays in the
      report index as history), then rewrite the README Performance block from the generated report
- [ ] Push everything; remove temporary worktrees (protoCore-pslo, protoCore-desc, protoCore-mpsc)
- [ ] Installers for the whole family (Linux, macOS, Windows), protoCore installed on the same machine:
      protoCore 2.0.0 as the base package; protoScala, protoST, protoClojure, protoPython and protoJS
      each declaring it as a runtime dependency (deb Depends / rpm Requires), never bundling libprotoCore;
      INSTALL_RPATH $ORIGIN/../lib so no LD_LIBRARY_PATH is needed. Linux (.deb/.rpm/.tar.gz) built AND
      verified here (install into a scratch prefix, run the binary); macOS (.dmg) and Windows (NSIS/.zip)
      configured but explicitly marked unverified on this build host.

## Overnight run 2026-09-23 — state of the closing sequence

Status of each item above, as verified at the end of the unattended run.
Everything below is **committed locally and nothing is pushed**.

- [x] Merge protoCore `feature/pslo-p1` then `feature/descendant-of` into
      master — 2.0.0, SOVERSION 2 (`ce529cfe`, `31cb9277`, `bf972d3f`),
      plus `50342ceb` adding protoScala to the README ecosystem table.
- [x] protoCore full suite 412/412, ASan clean, no perf regression.
- [x] Clean rebuild + suites + fixes of every embedder:
      - protoPython `f7557d5c` — 556/557; class chain is exactly the MRO.
      - protoJS `7d28bd16e` — integrity levels moved to a per-object
        `__integrity__` bitmask; test262 Object/Reflect/Proxy 3617 → 3619,
        0 regressions.
      - protoST `4b47b48` — 833/833, matching the README baseline exactly.
      - protoClojure `4ea4e70` — 383/383 (the README's 382 was pre-existing drift).
      - protoScala `3703759` — 694/694 in four configurations.
- [x] Phase 5 (actors and futures) — complete, benchmarked.
- [ ] **Phase P2 (`ProtoMPSCQueue`) — implemented but NOT merged. See below.**
- [x] Re-measure the suite and cold start — done, interleaved (`646ff28`).
      Cold start MET: every one of 21 samples per row under 25 ms.
      Geomean vs CPython 0.86x (was 0.83x) — inside spread, not a real move.
      Caught first: `build_bench` was linked against the stale protoCore 1.0.0
      in `/usr/local/lib` and crashed on `--version`; rebuilt clean against 2.0.0.
- [ ] Installers — plan written, not implemented:
      `docs/plans/2026-09-23-phase-i-installers.md`.
- [ ] Push everything; remove the temporary worktrees.

### Blocking the P2 merge (maintainer decision required)

`feature/mpsc-queue` (`b241ecab`, protoCore 2.1.0) is correct and fast:
433/433, TSan clean in queue code, ASan clean, the 8 x 1M stress consumes
exactly what it pushes, and a send at 8 producers costs 288 ns against
protoST's 3 729 ns. The retain-chain GC design is validated by a test that
**fails without it** (6 of 26 runs, 3 with real corruption), not by argument.

It is nevertheless not merged, because decision **D3 is wrong as
implemented**. `takeAll` guards its own critical section, but
`ProtoContext::newList(n, items)` opens a `CriticalSection` across the whole
list build, so a large batch leaves the consumer unparkable and P1 rises from
~20 us to tens of milliseconds per cycle (A/B-isolated twice). That violates
binding constraint 1 of `docs/platform/PMQ-SPEC.md` §3: no new stop-the-world
work proportional to queue length. The fix belongs in protoCore's bulk list
builder — an existing model — so it was recorded rather than taken.

Also open on that branch: no "before" performance numbers on the base commit,
and no embedder rebuilt against the added ABI field.

### Decisions taken by agents, pending review

Recorded as "[agent, pending review]" in `docs/DECISIONS-LOG.md` (D43-D52 and
A0-1..A0-11 for Phase 5), in `docs/platform/PMQ-SPEC.md` §7 (D1-D10 for P2),
and in `docs/plans/2026-09-23-phase-i-installers.md` Task 0 (D-I1..D-I7 for
the installers). The installer premise worth challenging first is **D-I1**:
protoCore never emits `install(EXPORT ...)`, so no consumer can check its
version or ABI, and the stale `SOVERSION 1` copy in `/usr/local/lib` on this
host would be accepted silently. Every runtime is proposed to move to
`find_package(protoCore 2.0 REQUIRED CONFIG)`.

### Bugs the implementation found in its own approved plan

Phase 5's plan contained three defects that only surfaced in code:
`Mailbox::push` held a CAS snapshot in a C++ local across `appendLast`
(a use-after-free that segfaulted with 8 senders under a heap limit);
`complete()` let a losing completer overwrite the winner's value; and
`awaitBlocking` read the wake epoch after the state check, losing every
wake-up (a `priority` run went from 7.4 s to 0.11 s once fixed). Plan
pseudocode is not a specification of correctness.

### Known incomplete

- ThreadSanitizer was never run against protoScala's actors.
- Cold start is not claimed: it straddled the 25 ms target depending on load.
  D44 grew the prelude; it is not eager worker creation (verified: 2 vs 12
  `clone3`).
- `fan-out` gets *worse* with more workers, where protoClojure improves.
  protoScala's mailboxes are still the CAS-list fallback rather than
  `ProtoMPSCQueue` — a plausible but **unconfirmed** explanation.
