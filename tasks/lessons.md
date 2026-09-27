# Lessons

One entry per correction, with the rule it produces. Reviewed at the start of a
session.

## Never write "Scala refuses this too" without running scalac

**Track S, 2026-09-25.** While refusing a default parameter value in an
overloaded top-level `def`, I wrote — in the code comment, the error message and
the fixture header — that scalac refuses it as well, citing Scala 2's "multiple
overloaded alternatives of method f define default arguments". Then I ran it:
scalac 3.9 **compiles** `def f(x: Int) = 1; def f(x: Int, y: Int = 2) = 3; f(1)`
and prints `1`, because it resolves the call by type. The restriction is
protoScala's, and claiming Scala's authority for it would have made a design
decision look like conformance.

**Rule.** Any sentence of the form "Scala also ...", "scalac rejects this too" or
"this matches Scala" is a claim about an observable, so it gets an observation:
`tools/scala3-3.9.0/bin/scalac -d out` then
`java -cp "$SCALA_HOME/lib/*:out"`, and the actual output quoted. Knowledge of
Scala 2 does not transfer to Scala 3. This applies hardest where the claim is
convenient, because a restriction is easier to defend as Scala's than as ours.

## Pin a rule against the reference before choosing which condition to add

**Track S, 2026-09-25.** A blank line before a leading infix operator changed a
program's answer. Two conditions were plausibly missing from the offside rule,
dotty's `!pastBlankLine` and an indentation guard, and scalac's diagnostic ("Line
is indented too far to the left") pointed at the second. A table of six shapes run
through scalac showed the opposite: the indentation is only a warning and the line
still continues, so an indentation condition would have broken a passing fixture,
and the blank line matters even where the next line is indented *more*, which
needed a second change nobody had predicted.

**Rule.** When a fix could be any of several conditions, build the truth table
against the reference implementation first and let it choose. Six scalac runs cost
minutes; guessing costs a wrong fixture that then defends the wrong behaviour.

## A fixture that cannot go red defends nothing

**Track S, 2026-09-25.** A pre-existing fixture, `override-val-parameter.scala`,
was written for exactly the bug the Scala 3 corpus found and could never have
caught it: its superclass read a bare `v`, which resolves to the constructor's own
parameter slot and never touches the attribute the bug corrupts. It would have
passed with no field stores emitted at all.

**Rule.** Every fixture is checked by naming a mutation of the implementation and
watching it turn red. A fixture for an *ordering* bug must read through the path
the ordering affects — `this.v`, not `v`. And an `XFAIL` documenting a gap is only
red when the program *conforms*, so a mutation that produces a different wrong
answer does not exercise it; a permanent deviation is better pinned with `EXPECT`
on our answer and the reference's quoted in the header.

## Do not `git checkout` a file to undo a mutation

**Track S, 2026-09-25.** Reverting a deliberate mutation with
`git checkout <file>` discarded the *fix* in that file as well, because the fix
was not yet committed. It went unnoticed until the fixture kept failing after the
"restore".

**Rule.** Copy the file aside before mutating it and copy it back, or commit the
fix first. `git checkout` restores the last commit, which during mutation testing
is exactly what you do not want.

## A plan's design can be wrong in a way only the harness finds

**Phase 7, 2026-09-26.** The plan said `gen::makeFn` should store the
`proto::ProtoMethod` "where the interpreter stores `__code__`". I read that,
believed it, and wrote it — and it cannot work: `compiledModuleOf` reads `__code__`
as a `BytecodeModule*`, and **nineteen** call sites depend on that reading. Worse,
the failure is silent. `Try.apply` refused a transpiled function; a `Map`'s
arity-deciding `map` read its arity through `compiledModuleOf`, got `nullptr`,
defaulted to **1**, and called a two-parameter lambda with one argument; a by-name
parameter was never forced, so a `Function` object reached arithmetic. None of it
raised anything that pointed at the cause. I did not reason my way to any of the
four — the differential harness printed them.

**Rule.** When a plan specifies a representation, the first thing to write is not
the representation: it is the differential that would notice the representation is
wrong. Build the comparison before the thing being compared, and let it tell you
what the design costs. And when a plan's step says "store X where Y is stored",
grep for every reader of Y first — the count is the size of the decision.

## "Excluded" and "passed" must never be the same number

**Phase 7, 2026-09-26.** The first differential sweep reported 858 of 919 passing.
475 of those "passes" were fixtures the transpiler had *refused*, counted as passes
because they were on the exclusion list. A reader — including me, an hour later —
reads 858/919 as coverage. It was 383.

**Rule.** A harness that can pass for two different reasons reports two numbers, and
the narrower one goes first. `transpiled OK` and `transpiled EXCLUDED` print
different words for exactly this reason, and every summary of the run states the
excluded count beside the passed count. A single percentage over a set that
contains refusals is a figure that will be quoted wrongly.

## A verification corpus can be satisfied vacuously

**Phase 7, 2026-09-26.** The rule was "every corpus test the interpreter passes must
also pass transpiled". It came out **0 divergences**, which reads like a clean bill.
It was not: of the 191 tests the interpreter passes, the transpiler **refused 186**
and ran 5. The rule held because almost nothing was measured.

**Rule.** A differential reports its **denominator**, not only its divergence count.
"0 divergences" means nothing until "over how many" is beside it, and a rule
satisfied by refusing the input is satisfied vacuously — which must be said in the
same sentence as the zero.

## A harness's own adaptation can be the thing under test

**Phase 7, 2026-09-26.** The corpus harness prepends a Predef shim so tests can use
`assert`. Two of the shim's declarations are **classes**. Since the first cut of the
transpiler refuses a class, the shim alone refused every corpus test, and the
"transpiled" column measured the shim rather than the corpus.

**Rule.** When reusing a harness, list what it *adds* to the input and ask whether
the new measurement is sensitive to any of it. Run the comparison with the
adaptation and without it, report both, and name the artefact rather than picking
the flattering row.

## A load average is not a quiet host

**2026-09-26.** I propagated a `MISSED` cold-start verdict into seven documents. It
was wrong. The measurement it rested on gated on a **load average of 1.84** while its
own `mpstat` in the same report never put the foreign load below **2.3 of 12 busy
CPUs** — the two metrics were both in the document and they disagreed, and the verdict
took the flattering one. On a host at **0.55** busy CPUs the same suite exits 0 in 24
of 24 cells at 21.63 ms. The control is decisive and costs nothing: the **same
unrebuilt binary** read 28.37 ms loaded and 22.16 ms quiet.

**Rule.** A load average counts runnable *and* uninterruptible tasks and lags by
design, so it is not a measure of available CPU. Any timing claim states **measured
idle** — `mpstat`'s `%idle`, or busy CPUs of N — at the start, the middle and the end.
And when a report carries two metrics that disagree about whether the host qualified,
that disagreement is the finding: stop and re-measure, do not pick one.

## Never mutate a file whose fix is uncommitted — I did it again

**2026-09-26.** `tasks/lessons.md` already carried this rule, from Track S. I
mutated `src/main.cpp` to check that a new test could fail, then reverted with
`git checkout -- src/main.cpp`, which discarded the **fix** along with the mutation,
because the fix was not committed. I noticed only because the test then passed for the
wrong reason.

**Rule, restated because reading it once was not enough:** copy the file aside first
(`cp x "$SCRATCH/x.fixed"`) and restore from the copy. `git checkout` restores the last
*commit*, which during mutation testing is precisely what you do not want. If a rule in
this file has already been broken once, put the mechanism in the command, not in the
intention.

## A test whose oracle is the code under test proves nothing

**2026-09-26.** `--version` had misreported the prelude image since `3fa7ff0`. My
first regression check compared `--version` **with and without**
`PROTOSCALA_PRELUDE_NO_IMAGE=1` and required the two lines to differ. A mutation that
made `--version` always report "no precompiled image in this build" — which is the bug
that actually shipped — **passed** it, because both lines then agreed. The oracle was
another reading of the same broken code path.

**Rule.** A regression check needs a source of truth *outside* the thing it checks.
Here it was `PROTOSCALA_PRELUDE_TIMING`'s `image=` flag, set by the code that actually
chooses the path; the check now compares the report against the behaviour. Before
writing the assertion, name what would tell you the answer if the feature under test
were lying — and if the answer is "the feature", find something else.

## An `exists()` check is not an "is usable" check

**2026-09-26.** `benchmarks/run_benchmarks.py` selected a Release column by
`Path.exists()`. `build_bench/protoscala` existed and was linked against the retired
`libprotoCore.so.2`, so it died in the dynamic loader — and was selected, timed at
4.45 ms, and only caught because the harness verifies *printed work* rather than exit
codes. The verification saved the table; the selection should never have happened.

**Rule.** A harness that discovers a binary must prove it **runs**, not that it is
present: `--version` is the cheapest universal probe and a loader failure fails it.
This matters most for sibling runtimes, whose build trees go stale far more often than
one's own.

## The heaviest stress case can be the one that hides the bug

**2026-09-26.** ThreadSanitizer reported a race in `finishTurn`'s post-release
re-check, once, during the 8 × 25,000-send stress case. I reached for that same case to
reproduce it — 4, 8 and 16 workers, a temporary assertion in place — and got **zero
firings**. The reason is structural: with senders hammering one actor, `sched` is 2 at
the end of nearly every turn, so `finishTurn` takes the branch that reads *under* the
claim and never enters the unprotected window. The window needed the **opposite**
shape: many actors each taking a short burst, so a turn ends with nothing queued and
the next message arrives just after the release. Six senders trickling over 64 actors
entered it ~18,000 times a run.

**Rule.** Before reproducing a concurrency report, read the code for *which branch*
the window is in, and construct the workload that takes that branch. A heavier load is
not a more likely reproduction; it can systematically take the safe path. And count
the window, not only the violation — instrumentation that reports "how often the risky
branch ran" is what tells you a zero means "safe" rather than "never tried".

## Widen a detector before believing its zero

**2026-09-26.** My assertion sampled the owner flag at *entry* to `hasWork` and
reported 0 overlaps in ~20,000 window entries, on every run. `hasWork` loops over three
bands doing attribute reads, and a second thread can claim the actor part way through,
so entry-sampling could only catch an overlap that had already started. Checking at
entry **and** exit found 2 firings in 6 runs.

**Rule.** A detector for a window must span the window. When an instrumented run
reports a suspiciously clean zero over a large number of opportunities, the first
hypothesis is the detector, not the code — and the cheapest test is to make the
detector cover more of the operation and see whether the zero survives.

## A demonstration that leaves the scaffolding in place demonstrates the scaffolding

**2026-09-27.** The cross-runtime-call test is supposed to prove that a foreign runtime
needs nothing but protoCore to call a transpiled protoScala function. My first version
loaded the module and called the function with the host's `ExecutionEngine::ActiveCallGuard`
still open around the callback. It passed on the first run. It proved nothing: with that
guard open, the call *is* running inside protoScala, which is exactly the situation the
test exists to rule out. The same shape had already appeared twice in this project — the
corpus differential's `full` shim mode reads 0 because the shim's own Predef declaration
is refused, so that row measures the shim and not the corpus.

**Rule.** When a test is supposed to prove that something is *not needed*, remove it and
watch the test fail before removing anything else. State the property as a mutation
(`X2`: delete the fallback that makes the call work without a guard) and require the red
run. And when a harness prepends its own scaffolding to every case, check what the
scaffolding alone does before reading any number the harness reports.

## A green mutation names the test you did not write

**2026-09-27.** Four mutations of `CompiledModuleProvider`; three turned
`cli/compiled-provider` red and one — reversing the resolution-chain order so a
compiled module would win over a source module — left it **green**. The test does cover
"a `.scala` beside a `.so` wins", so the property looked tested. It was not: `Session::load`
asks the source loader itself and reaches the chain only on a miss, so protoScala's own
importer never observes the order at all. The order decides for every *other* runtime,
which arrives through protoCore's `getImportModule` — a path no protoScala test
exercised.

**Rule.** Treat a mutation that stays green as a finding, not a dud: it has located a
claim whose test goes through a different code path than the claim. Fix it by asserting
the invariant where it lives — here, on the chain contents — rather than by deleting the
mutation or weakening the claim. And when a documented ordering is consumed by a
*foreign* caller, no test driven through our own front door will ever see it.

## A test must not encode the machine it was written on

**2026-09-27.** I gave two new test scripts a guard around their `rm -rf "$SCRATCH"`:

```bash
case "$SCRATCH" in
    /home/*/Documentos/proyectos/*) rm -rf "$SCRATCH" ;;
    *) echo "FAIL: refusing to clear '$SCRATCH'"; exit 1 ;;
esac
```

It came from a real safety rule — never delete outside the workspace — but it describes
*where the author works* rather than *what makes a path safe*. On CI the build tree is
`/home/runner/work/...`, so the guard refused and both scripts exited 1 in 0.00 s having
done nothing. The two tests that exist to demonstrate the phase's headline capability
reported failure while the capability worked perfectly, and the red read as "the feature
is broken on a clean machine" — the most misleading way for a guard to be wrong.

**Rule.** A safety check inside a committed test states a property of the path (absolute,
no `..`, deep enough that `/` and `/home` cannot be the target, not a symlink), never a
prefix of one machine's filesystem. A personal safety rule constrains what *I* type in a
shell; encoding it into a test exports my environment as a requirement. And before
committing any script that takes a path from the harness, check the decision against a
path shaped like CI's — a green local suite cannot see this class of bug at all.

## A mutation matrix whose target set is chosen by eye measures the eye

**2026-09-27.** I ran four mutations of the retry loop with
`ctest -R "Guarded\.|cli/transpiler-cli|transpiled/20-exceptions/..."`. Two came back
GREEN and looked like holes in the tests. They were not: `Guarded.` matched **no ctest
case at all**, because `test_generated_support` is registered as the single case
`unit/generated_support` rather than through `gtest_discover_tests`, so the four cases
written specifically to cover those two mutations were never run. The mutations were fine
and the tests were fine; the *filter* was wrong, and a wrong filter reports exactly what a
missing test reports.

**Rule.** A mutation records, as data, which ctest expression it must turn red —
`tests/mutations/apply.py` now has a `COVERS` map and a `covers <id>` subcommand, and the
runner asks it instead of guessing. Before believing any green mutation, check that the
filter selected a non-zero number of tests (`ctest -N -R <expr>`); a matrix that ran zero
cases is indistinguishable from a matrix that passed.

## Install it and run it, or the packaging is untested

**2026-09-27.** Task 14 Step 5 — install into a scratch prefix, then run what was
installed — found **two defects nothing else had noticed**, one of them present since the
shared library was added:

1. `libprotoScala.so*` was in CMake's `Unspecified` install component, because in
   `install(TARGETS)` an option after an artifact keyword binds to *that* artifact group
   and the single trailing `COMPONENT protoScala` had attached itself to `PUBLIC_HEADER`.
   So `cmake --install --component protoScala` installed the compiler and its header but
   not the library they link. The whole suite was green throughout, because every test ran
   from the build tree.
2. The generated `Makefile`'s `-I<prefix>/include` *adds* to the compiler's default search
   path rather than replacing it, so with no protoCore headers in the prefix a module
   compiled against `/usr/local/include/protoCore.h` — a months-old copy of a different
   protoCore. It linked (the library was right; only the header was wrong) and segfaulted
   in `ProtoObject::newChild` with no diagnostic.

**Rule.** A green suite that only ever runs from the build tree says nothing about what an
installation does. Install into a scratch prefix and *execute the installed binaries* with
`env -u LD_LIBRARY_PATH`, and check with `ldd` that every library resolves inside the
prefix. And when a tool hands work to a compiler, remember that `-I` and `-L` **add** to
default paths: the tool must verify the headers it depends on are reachable through its own
list, because a wrong header that happens to be on the system path fails at run time, not
at link time.
