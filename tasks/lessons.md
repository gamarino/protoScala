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
