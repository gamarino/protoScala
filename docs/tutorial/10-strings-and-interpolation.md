# 10. Strings and Interpolation

> **Implementation status.** Everything in this chapter runs today: string and
> triple-quoted literals, the `s`, `f` and `raw` interpolators with the full
> specifier table of §10.4, `stripMargin`, `format`, and the `String` method
> surface of §10.6. What is not implemented: a user-defined interpolator, which
> needs extension methods (§10.8, D56), `%n` and locales in `f` (D55), and
> regular expressions anywhere — `split` takes a literal separator (D70).

Chapter 4 introduced string literals and `+`. This chapter is about the two
things you will actually spend your time on: building a string out of values,
and taking a string apart.

Building is the part Scala does differently from most languages, and better
than concatenation. `"Hello, " + name + "! You are " + age + "."` works, but
the quotes and the pluses fragment the sentence. An **interpolator** puts the
values inside the sentence:

```text
s"Hello, $name! You are $age."
```

The `s` in front of the quote is the interpolator's name, `$name` is a hole,
and the result is one string. Python calls this an f-string, JavaScript a
template literal, and Scala had it first.

## 10.1 Literals

Fixture: [`tests/conformance/tutorial/10-strings-literals.scala`](../../tests/conformance/tutorial/10-strings-literals.scala)

```scala
@main def run(): Unit =
  val plain = "Hello, World"
  val quoted = "she said \"hi\""
  val escaped = "a\tb"
  println(plain + " | " + quoted + " | " + escaped.split("\t").mkString("<tab>") + " | " +
    plain.length)
```

Prints:

```text
Hello, World | she said "hi" | a<tab>b | 12
```

A string literal is in double quotes and understands the usual escapes: `\n`,
`\t`, `\"`, `\\`, `\r`, `\b`, `\f`, `\'`, and `\uXXXX` for a code point. A
`Char` literal is in single quotes — `'a'` is a `Char`, `"a"` is a `String` —
and `"a" + 'b'` is `"ab"`, because `+` on a string concatenates whatever
follows it.

A string is immutable, like every other value here: there is no operation that
changes one, only operations that answer a new one.

## 10.2 Triple-quoted strings

Fixture: [`tests/conformance/tutorial/10-strings-triple-quoted.scala`](../../tests/conformance/tutorial/10-strings-triple-quoted.scala)

```scala
@main def run(): Unit =
  val block = """a "quoted" word and a \n that is not an escape
and a second line"""
  println(block.split("\n").mkString(" / "))
```

Prints:

```text
a "quoted" word and a \n that is not an escape / and a second line
```

Between `"""` and `"""` everything is literal: quotes need no escaping, a
backslash is a backslash, and a newline in the source is a newline in the
string. That is the literal to use for a block of text, a piece of JSON or a
usage message — and §10.5 shows how to indent it without indenting its
contents.

## 10.3 `s"…"`: the interpolator you will use

Fixture: [`tests/conformance/tutorial/10-strings-s-interpolator.scala`](../../tests/conformance/tutorial/10-strings-s-interpolator.scala)

```scala
@main def run(): Unit =
  val name = "Ada"
  val age = 36
  println(s"$name is $age, and next year ${age + 1}")
```

Prints:

```text
Ada is 36, and next year 37
```

There are two forms of hole:

- `$name` — a single identifier. It stops at the first character that cannot be
  part of a name, so `s"$name!"` works and needs no delimiter.
- `${expr}` — any expression, including a method call, an arithmetic
  expression, or a `match`. Use it whenever the hole is not one plain name:
  `s"$name.length"` reads the name and then a literal `.length`, while
  `s"${name.length}"` is `3`.

Fixture: [`tests/conformance/tutorial/10-strings-s-expressions.scala`](../../tests/conformance/tutorial/10-strings-s-expressions.scala)

```scala
case class Point(x: Int, y: Int)

@main def run(): Unit =
  val p = Point(1, 2)
  val xs = List(1, 2)
  val price = 5
  println(s"$p $xs ${xs.length} ${p.x + p.y} $$$price")
```

Prints:

```text
Point(1,2) List(1, 2) 2 3 $5
```

Every hole is rendered with the value's `toString`, so a case class prints as
`Point(1,2)` and a list as `List(1, 2)`; your own `toString` is used when you
write one. `$$` is a literal dollar, which is why `$$$price` is `$5`.

An interpolated literal may span lines, may be triple-quoted
(`s"""…$x…"""`), and a hole may itself hold an interpolation
(`s"outer[${s"inner $n"}]"`). Holes also capture like any other expression: a
lambda that reads `$v` sees the variable, not a copy of its value at the time
the lambda was made.

## 10.4 `raw"…"`: no escapes

Fixture: [`tests/conformance/tutorial/10-strings-raw.scala`](../../tests/conformance/tutorial/10-strings-raw.scala)

```scala
@main def run(): Unit =
  val n = 2
  println(raw"C:\new\table $n" + " | " + s"C:\\new\\table $n")
```

Prints:

```text
C:\new\table 2 | C:\new\table 2
```

`raw` interpolates holes exactly as `s` does and leaves escape sequences
alone: in `raw"C:\new"` the `\n` is a backslash and an `n`, not a newline. The
two halves of the program above produce the same string, and the `raw` half is
the one you can read. Use it for Windows paths and for any text where
backslashes are data.

## 10.5 `f"…"`: formatted output

Fixture: [`tests/conformance/tutorial/10-strings-f-basics.scala`](../../tests/conformance/tutorial/10-strings-f-basics.scala)

```scala
@main def run(): Unit =
  val pi = 3.14159
  val n = 42
  println(f"pi=$pi%.2f n=$n%05d plain=$n done=$n%%")
```

Prints:

```text
pi=3.14 n=00042 plain=42 done=42%
```

`f"…"` is `s"…"` with a **format specifier** allowed after each hole. The
specifier is `printf`'s: `%[flags][width][.precision]conversion`. A hole with
no specifier is formatted as `%s`, so `plain=$n` still works, and `%%` is a
literal percent sign.

The conversions:

| Conversion | Formats | Example |
|---|---|---|
| `%s` | anything, through `toString` | `f"$xs%s"` |
| `%b` | a `Boolean` | `f"$flag%b"` |
| `%c` | a `Char` | `f"$c%c"` |
| `%d` | an integer, exactly, at any size | `f"$n%d"` |
| `%o` | an integer in octal | `f"$n%o"` |
| `%x`, `%X` | an integer in hexadecimal, lower or upper case | `f"$n%x"` |
| `%e`, `%E` | scientific notation | `f"$x%.3e"` |
| `%f` | fixed-point decimal (6 digits by default) | `f"$x%.2f"` |
| `%g`, `%G` | the shorter of `%e` and `%f` | `f"$x%g"` |
| `%%` | a literal `%` | `f"$n%%"` |

The flags, which come between the `%` and the width:

| Flag | Effect |
|---|---|
| `-` | left-align inside the width |
| `+` | always print a sign |
| (space) | print a space where a `+` would go |
| `0` | pad a number with leading zeros |
| `,` | group the integer part in thousands, with an ASCII comma |
| `#` | alternate form: `0x` before `%x`, `0X` before `%X`, `0` before `%o` |

Width is a minimum, precision a maximum: for `%f`/`%e`/`%g` the precision is
the number of digits after the point, and for `%s` it truncates.

Fixture: [`tests/conformance/tutorial/10-strings-f-alignment.scala`](../../tests/conformance/tutorial/10-strings-f-alignment.scala)

```scala
@main def run(): Unit =
  val left = "left"
  val right = "right"
  println(f"|$left%-10s|$right%10s|${"hello"}%.3s|")
```

Prints:

```text
|left      |     right|hel|
```

That is how a column of output is aligned: `%-10s` pads on the right, `%10s` on
the left, and `%.3s` truncates.

Fixture: [`tests/conformance/tutorial/10-strings-f-integers.scala`](../../tests/conformance/tutorial/10-strings-f-integers.scala)

```scala
@main def run(): Unit =
  val n = 1234567
  val h = 255
  val small = 7
  println(f"$n%,d $h%x $h%X $h%o $small%+d $small% d ${true}%b ${'a'}%c")
```

Prints:

```text
1,234,567 ff FF 377 +7  7 true a
```

Fixture: [`tests/conformance/tutorial/10-strings-f-floating.scala`](../../tests/conformance/tutorial/10-strings-f-floating.scala)

```scala
@main def run(): Unit =
  val x = 1234.5678
  println(f"$x%f $x%.1f $x%e $x%E $x%.3e $x%g")
```

Prints:

```text
1234.567800 1234.6 1.234568e+03 1.234568E+03 1.235e+03 1234.57
```

`%f` with no precision gives six decimals, `%e` six significant decimals, and
`%g` six significant digits in total — the same defaults `printf` has.

`%d` is exact at any size, because an integer here never overflows (D1). The
floating conversions are not:

Fixture: [`tests/conformance/tutorial/10-strings-f-big-integer-rounds.scala`](../../tests/conformance/tutorial/10-strings-f-big-integer-rounds.scala)

```scala
@main def run(): Unit =
  val big = 9007199254740993L
  println(f"$big%d $big%.0f")
```

Prints:

```text
9007199254740993 9007199254740992
```

`%.0f` converted the integer through a `Double`, whose 53 significant bits
cannot hold it, so the last digit changed. Use `%d` for integers; that is
deviation D66, and §10.8 has the reason.

Two specifiers are rejected before the program runs.

Fixture: [`tests/conformance/tutorial/10-strings-f-bad-spec.scala`](../../tests/conformance/tutorial/10-strings-f-bad-spec.scala)

```scala
@main def run(): Unit =
  val n = 1
  println(f"$n%q")
```

It is rejected with:

```text
10-strings-f-bad-spec.scala:4:11: error: unsupported format specifier '%q'
```

Fixture: [`tests/conformance/tutorial/10-strings-f-bare-percent.scala`](../../tests/conformance/tutorial/10-strings-f-bare-percent.scala)

```scala
@main def run(): Unit =
  val x = 1
  println(f"50% of $x")
```

It is rejected with:

```text
10-strings-f-bare-percent.scala:4:11: error: conversions must follow a splice in an f-interpolator; use %% for a literal % (%n is not supported: write \n)
```

Both of those are compile errors in Scala too. What is *not* a compile error
here is a value of the wrong kind for its conversion, because there is no
static type to check it against (D4):

Fixture: [`tests/conformance/tutorial/10-strings-f-type-mismatch.scala`](../../tests/conformance/tutorial/10-strings-f-type-mismatch.scala)

```scala
@main def run(): Unit =
  val s = "x"
  println(f"$s%d")
```

It stops with:

```text
10-strings-f-type-mismatch.scala:4: error: IllegalArgumentException: %d expects an integer, got String
```

The same formatter is available as a method, for a format string that is not a
literal:

Fixture: [`tests/conformance/tutorial/10-strings-format-method.scala`](../../tests/conformance/tutorial/10-strings-format-method.scala)

```scala
@main def run(): Unit =
  println("n=%03d and %s".format(7, "text"))
```

Prints:

```text
n=007 and text
```

`format` and `f"…"` share one implementation, so the two can never disagree.

## 10.6 `stripMargin`

Fixture: [`tests/conformance/tutorial/10-strings-stripmargin.scala`](../../tests/conformance/tutorial/10-strings-stripmargin.scala)

```scala
@main def run(): Unit =
  val letter = """|Dear Ada,
                  |  two spaces of body indent survive
                  |Yours, Alan""".stripMargin
  println(letter.split("\n").mkString(" / "))
```

Prints:

```text
Dear Ada, /   two spaces of body indent survive / Yours, Alan
```

A triple-quoted literal keeps the indentation of the source, which is exactly
wrong when the literal is inside an indented method. `stripMargin` removes,
from each line, the leading whitespace up to and including the first `|`.
Indentation *after* the bar is kept, which is how the second line keeps its
two spaces. Pass another character to use it instead of `|`
(`stripMargin('#')`). It combines with interpolation: `s"""|…$x…""".stripMargin`
is the usual way to write a multi-line message.

## 10.7 The `String` surface

A string behaves like a sequence of characters:

Fixture: [`tests/conformance/tutorial/10-strings-as-a-sequence.scala`](../../tests/conformance/tutorial/10-strings-as-a-sequence.scala)

```scala
@main def run(): Unit =
  val s = "scala"
  println(s.length.toString + " " + s(2) + " " + s.charAt(0) + " " + s.head + " " + s.last + " " +
    s.toList + " " + s.map(_.toUpper).mkString + " " + s.filter(_ != 'a') + " " +
    s.takeWhile(_ != 'l'))
```

Prints:

```text
5 a s s a List(s, c, a, l, a) SCALA scl sca
```

`s(2)` and `s.charAt(2)` are the same read; `map` and `filter` answer a `List`
of `Char` and a `String` respectively, and `mkString` puts a `List[Char]` back
together.

Case and searching:

Fixture: [`tests/conformance/tutorial/10-strings-case-and-search.scala`](../../tests/conformance/tutorial/10-strings-case-and-search.scala)

```scala
@main def run(): Unit =
  val s = "Hello, World"
  println(s.toUpperCase + " " + s.toLowerCase + " " + "ada".capitalize + " " +
    s.indexOf("World") + " " + s.lastIndexOf("l") + " " + s.contains("lo") + " " +
    s.startsWith("He") + " " + s.endsWith("ld") + " " + "ABC".equalsIgnoreCase("abc") + " " +
    "abc".compareTo("abd"))
```

Prints:

```text
HELLO, WORLD hello, world Ada 7 10 true true true true -1
```

`indexOf` answers `-1` when the text is absent, as on the JVM. `compareTo` is
negative, zero or positive. `toUpperCase` and `toLowerCase` map ASCII letters
only (D13), so `"héllo".toUpperCase` is `HéLLO`.

Slicing and trimming:

Fixture: [`tests/conformance/tutorial/10-strings-slicing.scala`](../../tests/conformance/tutorial/10-strings-slicing.scala)

```scala
@main def run(): Unit =
  val s = "  helloworld  "
  println("[" + s.trim + "] " + s.trim.substring(5) + " " + s.trim.take(5) + " " +
    s.trim.drop(5) + " " + s.trim.init + " " + s.trim.stripPrefix("hello") + " " +
    s.trim.stripSuffix("world") + " " + "abc".reverse)
```

Prints:

```text
[helloworld] world hello world helloworl world hello cba
```

`stripPrefix` and `stripSuffix` remove the text if it is there and answer the
string unchanged if it is not — no `if` around them.

Splitting and replacing:

Fixture: [`tests/conformance/tutorial/10-strings-split.scala`](../../tests/conformance/tutorial/10-strings-split.scala)

```scala
@main def run(): Unit =
  val csv = "name,age,city"
  println(csv.split(",").toString + " " + csv.split(",").length + " " +
    csv.split(",").map(_.toUpperCase).mkString("|") + " " + csv.replace(",", " / "))
```

Prints:

```text
List(name, age, city) 3 NAME|AGE|CITY name / age / city
```

`split` answers a **`List`**, not an `Array` (D69), which is convenient — the
whole surface of chapter 8 is available on the result. The separator is a
**literal**, not a regular expression (D70); §10.8 has the consequence. An
empty separator raises
`IllegalArgumentException: String.split needs a non-empty separator`.

Converting:

Fixture: [`tests/conformance/tutorial/10-strings-conversions.scala`](../../tests/conformance/tutorial/10-strings-conversions.scala)

```scala
@main def run(): Unit =
  println("12".toInt.toString + " " + "12".toLong + " " + "3.5".toDouble + " " +
    "true".toBoolean + " " + 42.toString + " " + "ab".repeat(3))
```

Prints:

```text
12 12 3.5 true 42 ababab
```

`toInt` on text that is not a number fails, so wrap it in a `Try` when the
input is not yours (chapter 8, §8.7): `Try("x".toInt)` is a `Failure`.

The surface, as delivered:

| Group | Methods |
|---|---|
| Size and access | `length`, `apply(i)`, `charAt`, `head`, `last`, `init`, `isEmpty`, `nonEmpty` |
| Slice | `substring`, `take`, `drop`, `takeWhile`, `dropWhile`, `trim`, `stripPrefix`, `stripSuffix`, `stripMargin`, `reverse` |
| Search | `indexOf`, `lastIndexOf`, `contains`, `startsWith`, `endsWith`, `compareTo`, `equalsIgnoreCase` |
| Case | `toUpperCase`, `toLowerCase`, `capitalize` |
| Sequence | `toList`, `map`, `filter`, `foreach`, `mkString`, `count`, `exists`, `forall` |
| Build | `+`, `repeat`, `replace`, `split`, `format` |
| Convert | `toInt`, `toLong`, `toDouble`, `toBoolean` |

## 10.8 What differs from Scala 3 in this area

**D54 — `s"…"` and `raw"…"` compile to a concatenation opcode.** In Scala both
desugar to a call on `StringContext`, so redefining `StringContext` — or
importing one that shadows it — changes what `s"…"` means. Here the compiler
emits a `CONCAT` instruction over the pieces and the holes, and a user-defined
`StringContext` is never consulted. For the same input the two produce the same
string; only the strategy differs, and the strategy is what makes an
interpolation as cheap as a `+` chain.

**D55 — the `f` conversion set is a subset.** `%s %b %c %d %o %x %X %e %E %f
%g %G %%` are supported, with the flags `-`, `+`, space, `0`, `,` and `#` and a
`width.precision`. What is missing: **`%n`** — write `\n`, which the error
message in §10.5 says explicitly — and **locales**: `,` always groups with an
ASCII comma and the decimal point is always `.`, where the JVM's formatter
follows `Locale.getDefault()`. There is no locale database to consult, and
adding one is out of scope for an interpolator. `%h`, `%a`, `%t` and the
argument-index syntax (`%1$s`) are not supported.

**D56 is retired (Phase 4).** An interpolator other than `s`, `f` or `raw` is
lowered to `StringContext(<literals>).<name>(<args>)`, exactly as Scala lowers
it, so you supply it as an extension method on `StringContext`:

Fixture: [`tests/conformance/tutorial/10-strings-custom-interpolator.scala`](../../tests/conformance/tutorial/10-strings-custom-interpolator.scala)

```scala
extension (sc: StringContext)
  def shout(args: Any*): String =
    var out = sc.parts(0)
    var i = 0
    while i < args.length do
      out = out + args(i) + sc.parts(i + 1)
      i += 1
    out.toUpperCase

@main def run(): Unit =
  val who = "world"
  println(shout"hello $who")
```

```text
HELLO WORLD
```

`sc.parts` is the list of literal pieces — `List("hello ", "")` here — and
`args` the values of the holes, so `parts` always has exactly one more element
than `args` and an interpolator decides for itself how to weave them together.

An interpolator nothing defines is now a **run-time** error naming the member it
looked for, because types are erased (D4):

Fixture: [`tests/conformance/tutorial/10-strings-unknown-interpolator.scala`](../../tests/conformance/tutorial/10-strings-unknown-interpolator.scala)

```scala
@main def run(): Unit =
  val n = 1
  println(json"value is $n")
```

```text
10-strings-unknown-interpolator.scala:4: error: NoSuchMethodError: value json is not a member of StringContext
```

**D66 — `%e`, `%f` and `%g` convert through a `Double`.** An integer above
2^53 therefore prints rounded, as §10.5 showed. Matching the JVM would need a
big-decimal formatter; `%d` is exact at any size and is what an integer wants.
A `Double` is formatted identically to the JVM's for every value a `Double` can
hold.

**D69 — `split` answers a `List`, not an `Array`.** There is no `Array` type
(varargs already arrive as a `List`, D12), and adding one for `split` alone
would be a new collection with no other use. Code that writes
`s.split(",")(0)` or `.length` is unaffected; code that writes `.toSeq`,
`.toList` or passes the result where an `Array` is required must drop the
conversion. Printing it is *better* here: `println(s.split(","))` shows
`List(a, b, c)` where the JVM shows `[Ljava.lang.String;@1b6d3586`.

**D70 — `split` takes a literal separator, not a regular expression.**

Fixture: [`tests/conformance/tutorial/10-strings-split-is-literal.scala`](../../tests/conformance/tutorial/10-strings-split-is-literal.scala)

```scala
@main def run(): Unit =
  println("a.b.c".split(".").toString + " " + "a,,b".split(",") + " " + "abc".split(","))
```

Prints:

```text
List(a, b, c) List(a, , b) List(abc)
```

The first field is the divergence: Scala reads `"."` as the regular expression
"any character" and answers an empty sequence, while here `"."` is the
character `.` and the string splits on it. The common case, `split(",")`, is
identical; a separator that happens to be a regex metacharacter behaves
differently, and behaves the way a reader without regex knowledge would
predict. protoScala has no regular-expression engine and would not add one for
`split`; where you need one, split on a literal and filter, or write the scan
by hand.

**Smaller differences,** for completeness:

- **D13** — `length`, `charAt`, `substring` and `indexOf` count Unicode **code
  points**, where the JVM counts UTF-16 units. The two agree for every
  character in the Basic Multilingual Plane and differ for emoji and other
  astral characters, where protoScala's answer is the one you would want.
  `toUpperCase`, `toLowerCase` and `Char.toUpper`/`toLower` map ASCII only.
- Not implemented on `String`: `toVector`, `toSet`, `sorted`, `distinct`,
  `zip`, `matches`, `replaceAll`, `splitAt`, `linesIterator`, `toCharArray`.
  `toList` reaches the whole of chapter 8's surface when you need one of them —
  `"cba".toList.sorted.mkString` is `"abc"`.
- `"%s".format` and `f"…"` share the formatter, so `format` inherits D55 and
  D66 exactly.
- An interpolated literal is not a `StringContext` value: there is nothing to
  pass to a method that expects one (D54).

Everything else — the two hole forms, `$$`, `raw`'s escape handling, the
specifier grammar, `stripMargin`, the `String` methods listed in §10.7 — is
Scala 3's, and every fixture in this chapter was run against the binary that
this release ships.
