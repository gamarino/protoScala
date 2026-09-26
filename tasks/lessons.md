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
