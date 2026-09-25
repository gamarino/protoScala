# 16. Reading and writing files

> **Implementation status.** Everything in this chapter runs today:
> `Source.fromFile`, `Source.fromString`, `mkString`, `getLines()`, `close()`, and
> the four writing operations `FileIO.write`, `FileIO.append`, `FileIO.exists` and
> `FileIO.delete`. What is not implemented: any other encoding than UTF-8 (D99),
> an `Iterator` — `getLines()` answers a `List[String]` (D100) — directories
> (there is no `mkdir`, no listing and no rename), random access, binary files,
> and anything from `java.io` or `java.nio` (D8). Reading is `scala.io.Source`
> and behaves like it; writing is protoScala's own surface and is **not** a
> `PrintWriter` (D102).

A program that cannot read its own input is a program you have to paste data
into. This chapter is the two halves of fixing that: reading, which follows
Scala's `scala.io.Source` closely enough that Scala code you already have will
run, and writing, which does not follow Scala at all — and says why.

```text
Source.fromFile("notes.txt").getLines()
```

## 16.1 Reading a whole file

`Source.fromFile(path)` opens a file and reads it. `getLines()` gives you its
lines, without their line terminators; `mkString` gives you the whole text,
terminators and all. `close()` says you are done with it.

Fixture: [`tests/conformance/tutorial/16-files-write-then-read.scala`](../../tests/conformance/tutorial/16-files-write-then-read.scala)

```scala
FileIO.write("notes.txt", "milk\nbread\napples\n")

val src = Source.fromFile("notes.txt")
val lines = src.getLines()
val size = src.mkString.length
src.close()

println(s"${lines.length} lines, $size characters")
```

Prints:

```text
3 lines, 18 characters
```

`getLines()` answers a **`List[String]`**, so everything you know about `List`
applies to it directly — `map`, `filter`, `count`, `zipWithIndex`, a `for`
comprehension, all of it.

Fixture: [`tests/conformance/tutorial/16-files-read-a-file-you-were-given.scala`](../../tests/conformance/tutorial/16-files-read-a-file-you-were-given.scala)

```scala
FileIO.write("shopping.txt", "milk\nbread\napples\n")

// `getLines()` answers a List, so everything you know about List applies.
val shouted = for line <- Source.fromFile("shopping.txt").getLines() yield line.toUpperCase
println(shouted.mkString(" | "))
```

Prints:

```text
MILK | BREAD | APPLES
```

On the JVM, `getLines()` answers an `Iterator[String]` and you write `.toList` to
get a list. protoScala has no `Iterator` at all, so it answers the list (D100).
`.toList` on a `List` is the identity, which means the Scala spelling
`getLines().toList` also works and means the same thing.

### `mkString` and `getLines()` are the same file two ways

Fixture: [`tests/conformance/tutorial/16-files-mkstring.scala`](../../tests/conformance/tutorial/16-files-mkstring.scala)

```scala
FileIO.write("poem.txt", "one\ntwo\nthree\nfour\n")

val whole = Source.fromFile("poem.txt").mkString
// `mkString` is every byte of the file, newlines included; `getLines()` is the
// same text with the newlines removed and the pieces handed to you separately.
println(s"${whole.length} ${Source.fromFile("poem.txt").getLines().length}")
```

Prints:

```text
19 4
```

### How lines are counted

The details matter, because every one of them is a bug somebody has shipped.
protoScala answers exactly as Scala 3 on the JVM does, and each row below was
checked against `scalac` 3.9.0:

| The file contains | `getLines()` |
|---|---|
| `alpha\nbeta\ngamma\n` | `List(alpha, beta, gamma)` — the trailing newline adds no fourth line |
| `alpha\nbeta\ngamma` | `List(alpha, beta, gamma)` — a last line needs no terminator |
| *(nothing at all)* | `List()` — **no** lines, not one empty line |
| `\n` | `List("")` — one empty line |
| `a\n\nb\n` | `List(a, "", b)` — a blank line in the middle is a line |
| `a\r\nb\r\n` | `List(a, b)` — `\r\n` is one terminator |
| `a\rb\r` | `List(a, b)` — so is a lone `\r` |

`Source.fromString` reads text you already have through the same surface and the
same splitter, which is useful for testing a function that takes a `Source`:

```scala
Source.fromString("x\ny").getLines()     // List(x, y)
```

## 16.2 If you come from Python or JavaScript

Reading a file is one of the first things you will reach for, so here it is in
all three languages.

**Python**

```python
with open("notes.txt") as f:
    lines = f.read().splitlines()
```

**JavaScript (Node)**

```javascript
const fs = require("fs");
const lines = fs.readFileSync("notes.txt", "utf8").split("\n");
```

**protoScala**

```scala
val lines = Source.fromFile("notes.txt").getLines()
```

Four things to carry over:

1. **There is no `with` and no `try`-with-resources.** `close()` exists and you
   should call it when you keep a source around, but a source you read and drop
   holds nothing open: protoScala reads the file when it opens it. That is why
   the one-liner above is not a resource leak, and it is also the one place where
   protoScala's `Source` is deliberately not Scala's (§16.5).
2. **A missing file raises; it does not return `None`, `null` or `undefined`.**
   Python raises `FileNotFoundError`, Node throws an `Error` with `code:
   "ENOENT"`, and protoScala raises `FileNotFoundException`. See §16.4.
3. **`getLines()` does not leave you a trailing empty string.** Node's
   `.split("\n")` on a file that ends in a newline gives you a final `""` that
   you then have to remember to drop; Python's `.splitlines()` and protoScala's
   `getLines()` both do not.
4. **The encoding is not a choice.** Node makes you pass `"utf8"` or get a
   `Buffer`; Python guesses from your locale. protoScala reads UTF-8, always, and
   tells you when the bytes are not UTF-8 (D99).

Writing, side by side:

| | Python | Node | protoScala |
|---|---|---|---|
| replace a file | `open(p, "w").write(s)` | `fs.writeFileSync(p, s)` | `FileIO.write(p, s)` |
| add to the end | `open(p, "a").write(s)` | `fs.appendFileSync(p, s)` | `FileIO.append(p, s)` |
| is it there | `os.path.exists(p)` | `fs.existsSync(p)` | `FileIO.exists(p)` |
| remove it | `os.remove(p)` | `fs.unlinkSync(p)` | `FileIO.delete(p)` |

## 16.3 Writing: `FileIO`

Scala's canonical writer is `java.io.PrintWriter`, or `java.nio.file.Files` for
the whole-file operations. protoScala has **no Java interop and will not have
one** (D8), so there is nothing to imitate: simulating a `PrintWriter` would mean
inventing a stream hierarchy, a `Writer`, a `flush` and a buffering policy, all
of it so that one line of user code could look familiar. Instead there are four
operations, each one call, each doing exactly what its name says. This is a
deviation, and it has an id: **D102**.

```scala
FileIO.write(path, text)    // replace the file's contents, creating it if needed
FileIO.append(path, text)   // add to the end, creating it if needed
FileIO.exists(path)         // is there anything at this path
FileIO.delete(path)         // remove a file; false if there was nothing there
```

Fixture: [`tests/conformance/tutorial/16-files-append.scala`](../../tests/conformance/tutorial/16-files-append.scala)

```scala
FileIO.delete("journal.txt")
FileIO.append("journal.txt", "first\n")
FileIO.append("journal.txt", "second\n")
FileIO.append("journal.txt", "third\n")
println(Source.fromFile("journal.txt").getLines().mkString("|"))
```

Prints:

```text
first|second|third
```

`write` replaces the **whole** file. There is no writing at an offset and no
half-replaced file to worry about.

Fixture: [`tests/conformance/tutorial/16-files-write-replaces.scala`](../../tests/conformance/tutorial/16-files-write-replaces.scala)

```scala
FileIO.write("draft.txt", "before, and rather longer\n")
FileIO.write("draft.txt", "after\n")
// `write` replaces the whole file. There is no "write at position" and no
// half-replaced file: use `append` when you mean to add.
println(Source.fromFile("draft.txt").mkString.trim)
```

Prints:

```text
after
```

Text is written as **UTF-8**, which is what `Source.fromFile` reads back, so a
string survives the round trip unchanged:

Fixture: [`tests/conformance/tutorial/16-files-utf8.scala`](../../tests/conformance/tutorial/16-files-utf8.scala)

```scala
val text = "café €"
FileIO.write("utf8.txt", text)
val back = Source.fromFile("utf8.txt").mkString
// Text is written as UTF-8 and read back as UTF-8, so a length is a count of
// characters and not of bytes: 6 characters in 9 bytes.
println(s"${back.length} $back")
```

Prints:

```text
6 café €
```

### `exists` and `delete`

Fixture: [`tests/conformance/tutorial/16-files-exists-and-delete.scala`](../../tests/conformance/tutorial/16-files-exists-and-delete.scala)

```scala
FileIO.delete("scratch.txt")
val before = FileIO.exists("scratch.txt")
FileIO.write("scratch.txt", "x")
val after = FileIO.exists("scratch.txt")
val removed = FileIO.delete("scratch.txt")
// `delete` answers `true` when it removed a file and `false` when there was
// nothing at the path. It never answers `false` for a failure -- see the next
// section.
val again = FileIO.delete("scratch.txt")
println(s"$before $after $removed $again ${FileIO.exists("scratch.txt")}")
```

Prints:

```text
false true true false false
```

`exists` answers `true` for a directory too — it asks whether *anything* is at
the path — and `false` for a symbolic link that points at nothing, exactly as
`java.io.File.exists()` does.

`delete` removes a **file**. Asked to remove a directory it raises an
`IOException`; it does not answer `false`, because `false` means "there was
nothing there" and a program that could not tell those apart would be carrying on
with a wrong picture of the filesystem.

## 16.4 When it fails, and it will

File I/O fails constantly in real use, so every failure here raises something you
would think to catch, and every message names the path and says what went wrong.

| What happened | Class raised | The message |
|---|---|---|
| no such file | `FileNotFoundException` | `notes.txt (No such file or directory)` |
| a directory where a file was expected | `FileNotFoundException` | `. (Is a directory)` |
| no permission | `FileNotFoundException` | `notes.txt (Permission denied)` |
| the parent directory does not exist (on `write`) | `FileNotFoundException` | `a/b.txt (No such file or directory)` |
| the bytes are not valid UTF-8 | `MalformedInputException` | `bad.bin: malformed UTF-8 input at byte 1` |
| the source was closed | `IOException` | `notes.txt (Stream Closed)` |
| `delete` on a directory | `IOException` | `logs (Is a directory)` |
| the path contains a NUL | `IllegalArgumentException` | `a path may not contain a NUL character` |

The hierarchy is the JVM's with the `java.io.` and `java.nio.charset.` prefixes
dropped (D8): `IOException` extends `Exception`, and both
`FileNotFoundException` and `CharacterCodingException` extend `IOException`, with
`MalformedInputException` under the latter (D97).

Fixture: [`tests/conformance/tutorial/16-files-missing-file.scala`](../../tests/conformance/tutorial/16-files-missing-file.scala)

```scala
try
  println(Source.fromFile("there-is-no-such-file.txt").mkString)
catch
  case e: FileNotFoundException => println(s"${e.getClass}: ${e.getMessage}")
```

Prints:

```text
FileNotFoundException: there-is-no-such-file.txt (No such file or directory)
```

Catch `IOException` when you do not care which way it failed — that is the point
of having one class above all of them:

Fixture: [`tests/conformance/tutorial/16-files-one-catch-for-every-failure.scala`](../../tests/conformance/tutorial/16-files-one-catch-for-every-failure.scala)

```scala
// `IOException` is the one class to catch when you do not care which way it
// failed: a missing file, a directory where a file was expected, no permission
// and a file that is not valid UTF-8 are all IOExceptions.
def read(path: String): String =
  try Source.fromFile(path).mkString
  catch case e: IOException => s"caught ${e.getClass}"

println(s"${read("nothing-here.txt")}, ${read(".")}")
```

Prints:

```text
caught FileNotFoundException, caught FileNotFoundException
```

Nothing is ever swallowed. Reading a directory does not hand you an empty string:

Fixture: [`tests/conformance/tutorial/16-files-a-directory-is-not-a-file.scala`](../../tests/conformance/tutorial/16-files-a-directory-is-not-a-file.scala)

```scala
// "." is the working directory. Asking to read it fails, and says why -- it does
// not hand you an empty string.
println(Source.fromFile(".").mkString)
```

Fails with:

```text
error: FileNotFoundException: . (Is a directory)
```

And a file whose bytes are not UTF-8 is refused rather than decoded into
replacement characters, because a program that silently read a corrupted file
would never find out. Decoding is strict in the same way the JVM's is: an
overlong encoding, a surrogate code point, a value above U+10FFFF and a sequence
cut short at the end of the file are all malformed. The
[`tests/conformance/26-file-io/`](../../tests/conformance/26-file-io/) fixtures
pin each of those against committed files of exactly those bytes — this chapter
cannot show it as a snippet, because `FileIO.write` takes a `String` and can only
ever produce valid UTF-8.

There is one place where a failure is reported later than Scala reports it: a
type error. `Source.fromFile(42)` is a compile error in Scala and a
`ClassCastException` here, when the call runs, because protoScala erases types
(D4). Late **detection** is the price of a late-binding platform; a silent wrong
answer would not be acceptable, and is not what happens.

## 16.5 A closed source, and reading twice

Scala's `Source` wraps an open stream and is **consumed as it is read**: after
`src.mkString`, `src.getLines()` answers an empty list, because the underlying
iterator is exhausted. protoScala reads the file when it opens it, so it can
answer as often as you ask. That is **D101**, and it is deliberate: the Scala
behaviour is a reliable source of bugs, and matching it would mean carrying a
cursor for no gain.

Fixture: [`tests/conformance/tutorial/16-files-read-twice.scala`](../../tests/conformance/tutorial/16-files-read-twice.scala)

```scala
FileIO.write("twice.txt", "a\nb\nc\n")
val src = Source.fromFile("twice.txt")
// A protoScala source may be read again. On the JVM the second answer would be
// an empty list, because Scala's Source is consumed as it is read (D101).
println(s"${src.getLines().length} ${src.getLines().length}")
```

Prints:

```text
3 3
```

What does **not** change is that a closed source is closed. `close()` could have
been a no-op here, since nothing is held open — it is not, because a program that
reads a source it has already closed has a bug and should be told about it.

Fixture: [`tests/conformance/tutorial/16-files-a-closed-source.scala`](../../tests/conformance/tutorial/16-files-a-closed-source.scala)

```scala
FileIO.write("closed.txt", "text\n")
val src = Source.fromFile("closed.txt")
src.close()
try println(src.mkString)
catch case e: IOException => println(s"${e.getClass}: ${e.getMessage}")
```

Prints:

```text
IOException: closed.txt (Stream Closed)
```

## 16.6 A small program

Nothing new, just the pieces together: write a log, read it back, and count.

Fixture: [`tests/conformance/tutorial/16-files-word-count.scala`](../../tests/conformance/tutorial/16-files-word-count.scala)

```scala
FileIO.write("app.log",
  "INFO started\nERROR disk is full\nINFO retrying in 5 seconds\nERROR giving up\n")

val lines = Source.fromFile("app.log").getLines()
val errors = lines.count(l => l.startsWith("ERROR"))
val words = lines.map(l => l.split(" ").length).sum
println(s"errors=$errors words=$words")
```

Prints:

```text
errors=2 words=14
```

For a longer one, the [worked example](worked-example.md) reads its own
`sample.log` with exactly this surface.

## 16.7 What differs from Scala 3

| | Scala 3 on the JVM | protoScala | id |
|---|---|---|---|
| `getLines()` | `Iterator[String]` | `List[String]` — there is no `Iterator` | D100 |
| reading a source twice | the second read is empty; a `Source` is consumed | reads the file once, at `fromFile`, and answers as often as asked | D101 |
| encodings | any charset the JVM knows, from an implicit `Codec` | UTF-8 only; another name raises `UnsupportedOperationException` | D99 |
| writing | `java.io.PrintWriter`, `java.nio.file.Files` | `FileIO.write` / `append` / `exists` / `delete` | D102 |
| `MalformedInputException`'s message | `Input length = 1` | the path and the byte offset | D98 |
| the reason in a message | `strerror`, in your locale | the same text, always in English | D98 |
| exception class names | `java.io.FileNotFoundException` | `FileNotFoundException` | D8 |
| `Source` as an `Iterator[Char]` | `src.toList` gives characters, `src.next()` works | not provided; use `mkString` or `getLines()` | D100 |
| directories | `java.nio.file.Files.createDirectory`, listing, walking | none | — |
| binary files, random access | `FileChannel`, `InputStream` | none | — |

What is **the same**, and was checked against `scalac` 3.9.0 rather than assumed:
the names `Source.fromFile`, `Source.fromString`, `mkString`, `getLines()` and
`close()`; which exception class each failure raises; the `<path> (<reason>)`
shape of a failure message; every row of the line-splitting table in §16.1; and
that reading a closed source fails.

---

Previous: [15. Modules and polyglot interop](15-modules-and-polyglot-interop.md) ·
Next: [Worked example](worked-example.md) ·
Up: [Tutorial index](../TUTORIAL.md)
