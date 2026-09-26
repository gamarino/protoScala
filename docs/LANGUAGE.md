# protoScala Language Reference

> **Status:** target language definition (2026-09-23; Phases 1 and 2
> shipped). What is implemented today is tracked in
> [STATUS.md](STATUS.md). The reference language is
> **Scala 3** (the Scala 3 language reference); this document lists what
> protoScala accepts, the milestone that brings each feature, and every
> intentional departure.

## 1. Lexical syntax (Phase 1)

- Identifiers: alphanumeric (`x`, `fooBar`, `_x1`), operator identifiers
  (`+`, `::`, `<=`, `!`), mixed (`unary_-`, `x_+`), backquoted (`` `type` ``).
- Hard keywords: `abstract case catch class def do else enum export extends
  false final finally for given if implicit import lazy match new null object
  override package private protected return sealed super then this throw trait
  true try type val var while with yield`.
- Soft keywords: `as derives end extension infix inline opaque open transparent
  using`, plus `|`, `*`, `+`, `-` in their special positions.
- Literals: integers (decimal, hex, binary, `_` separators, `L` suffix
  accepted and ignored), floating point (`1.0`, `1e3`, `f`/`d` suffixes
  accepted), characters (`'a'`, escapes, `\u` unicode), strings (`"..."`,
  triple-quoted `"""..."""`), interpolators `s`, `f`, `raw`, booleans, `null`.
- Comments: `//` line, `/* ... */` block, **nested**.
- **Significant indentation** (Scala 3 offside rule) and braces, mixable as in
  Scala 3; optional `end` markers (`end if`, `end match`, `end MyClass`, ...).
- **Leading infix operator:** a line starting with an operator identifier, a
  blank and an operand continues the previous expression -- `val total = 1` then
  `+ 2` is `3`. A **blank line** ends the expression first, whatever the
  indentation of the operator line, so `val x = 1`, a blank line, `+ a * 6`
  leaves `x` at `1` and makes `+ a * 6` a statement of its own. Only whitespace
  makes a line blank; a comment-only line still continues the expression. This
  is Scala 3's rule exactly.

## 2. Expressions and definitions

| Feature | Phase |
|---|---|
| `val`, `var`, `lazy val`, `def` (multiple parameter lists, default and named arguments, varargs `xs: Int*`) | 1 ✅ (defaults/named: 4 ✅ — D81, D88, D89) |
| by-name parameters `x: => T` on a `def`, a method or a plain constructor parameter | 5 ✅ (D47, D53) |
| `if`/`then`/`else`, `while`/`do`, blocks as expressions, `return` | 1 |
| lambdas `x => e`, `(x, y) => e`, placeholder syntax `_ + 1` | 1 (placeholders: 2 ✅) |
| infix, prefix (`-x`, `!b`, `~n`) and postfix-free method application | 1 |
| string interpolation `s""`, `f""`, `raw""` | 3 ✅ |
| `for` comprehensions (generators, guards, value definitions, patterns, `yield` and `do`) | 2 ✅ |
| `match` with the patterns of DESIGN §5.3, pattern `val`s, `{ case ... }` literals | 2 ✅ |
| `try`/`catch`/`finally`, `throw`, with pattern-matched handlers | 4 ✅ (§3.1; D72–D75, D85–D87) |
| `import` (selectors, renames `as` and `=>`, wildcard `*` and `_`, `given` selectors parsed and ignored), and the family prefixes `py.`/`js.`/`st.`/`clj.` | 6 ✅ (§3.2; D90–D96) |
| `import Obj.*` / `.{a, b}` / `.a as b` on an object, companion or `enum` in scope — plain Scala's **member** import | Track X ✅ (§3.2; D105). Phase 6 had replaced it with the module-loading form |
| top-level definitions (no wrapping `object` needed), `@main` methods | 1 |
| `object Main extends App` as the program's entry point | Track X ✅ (§4.4; D104) |
| `package` clauses (one namespace per file) | — **not implemented and not scheduled**: a module is a file reached by its path, not a package (D91). `package p` is refused with "not implemented yet" |

### 2.1 By-name parameters

A parameter written `x: => T` is not evaluated at the call site. The caller
wraps the argument in a zero-argument thunk and every read of the name in the
body runs it, so an argument used twice evaluates twice and one never used never
evaluates — Scala's rule.

```scala
def unless(cond: Boolean)(body: => Int): Int = if cond then 0 else body
unless(true)({ println("never printed"); 1 })     // 0

var n = 0
def twice(body: => Int): Int = body + body
twice({ n = n + 1; n })                           // 3, and n is 2
```

Scala reads the by-name marker off the callee's *static type*. protoScala erases
types and dispatches dynamically, so the marker is only available where the
compiler can name the callee. It is honoured at a call by name to a top-level or
local `def` (any parameter list), a method of the template being compiled, a
method of an `object`, a class's primary constructor, and a builtin whose by-name
signature the runtime declares (`Future.apply`). Anywhere else the argument is
evaluated once at the call — see **D53**, which is a real departure from Scala,
not an implementation detail.

Two forms are rejected outright, as scalac rejects them: a by-name parameter on a
function literal (`(x: => Int) => x`), and a `val`, `var` or case-class
constructor parameter, which would have to hold a thunk instead of a value.

## 3. Classes and objects (Phase 2)

Delivered in Phase 2 ✅:

- `class`, constructor parameters (`val`/`var`/plain), auxiliary constructors
  `def this(...)`, `extends`, `with`, `override`, `abstract`, `final`,
  `sealed`, `open` (accepted), access modifiers (parsed; `private` enforced
  only as a lookup restriction on the defining class and its companion — D5).
- `object` (a lazily initialised singleton), companion objects,
  `case class`, `case object`, `trait` with concrete and abstract members,
  trait parameters (Scala 3), Scala's linearization.
- `super.m`, including stackable traits.
- `this`, self-type aliases (`self =>`), `isInstanceOf[T]`, `asInstanceOf[T]`
  (checked at run time where `T` denotes a class; unchecked for parameterised
  types — erasure; D29 and D37 for what the test actually compares).
- Case-class members `apply`, `unapply`, `equals`, `hashCode`, `toString`,
  `copy`, `canEqual`, `productArity`, `productElement`, `productPrefix`,
  `_1`..`_N`; tuples `Tuple2`..`Tuple22` as case classes (D32).
- The universal `apply` rule where the receiver has an `apply` member (a case
  class, or an `object`/companion that defines one), `update` (`a(i) = v`),
  generated setters and method values (D10). `C(args)` on a plain class with
  no companion `apply` is not rewritten to `new C(args)` — D41.

Delivered in Phase 4 ✅:

- `enum` with simple and parameterised cases, `values`, `ordinal`, `valueOf` and
  `fromOrdinal`, lowered entirely in the frontend to a sealed abstract class, one
  `case object` or `case class` per case, and a companion (D77, D79). A case is
  named `E.Case`, as in Scala. Three details follow Scala exactly: `case C` is a
  case **object** while `case C()` is a zero-parameter case **class**, so `E.C()`
  calls its companion's `apply`; a hand-written `object E` in the same file is the
  **same** companion the desugarer generates and is folded into it; and the enum
  may itself be called `Enum`.
- `type` aliases (Track S): `type X = T`, `type X[A] = T`, and the abstract forms
  `type X` and `type X >: L <: U`. Types are erased (D4), so an alias is a
  **naming** concern: the compiler records `X` -> its target and expands it
  wherever a type name is consumed -- a parent, a `new`, a type pattern, an
  `isInstanceOf` and an extension's receiver. Chains work; bounds and type
  parameters are parsed and discarded like every other. An alias is recorded
  unit-wide rather than per template (D109).
- Extension methods `extension (x: T) def m ...`, dispatched on the receiver's
  runtime prototype (D6) and global for the session (D82, D83). A custom string
  interpolator is one of these, on `StringContext` (D56 retired).
- `super[T].m`, which names the ancestor explicitly instead of taking the next one
  in the linearization (D76).
- Named and default arguments for Scala-defined methods, constructors, case-class
  `apply`/`copy`, function values and local functions, bound in the callee through
  protoCore's `keywordParameters` (D81, D88, D89; `docs/INTEROP.md` §7).
- Multiple constructor parameter lists, concatenated into one flat list (D84).
- Templates nested in an `object`, lifted to the top level with a qualified name.

Later phases:

- A `class`, `trait` or `object` nested in a **`class`** or **`trait`**, a local
  class inside a block, and anonymous classes `new T { ... }` (D80): each captures
  the enclosing instance, which needs a per-instance class. Anything else than
  top-level or `object`-nested is rejected with "classes, traits and objects must
  be defined at the top level of a file or in an object".

### 3.1 The exception hierarchy (Phase 4)

Twenty-six classes in `lib/prelude.scala`, the JVM's names without the
`java.lang.`, `java.io.` and `java.nio.charset.` prefixes (D73, D8, D97). They are ordinary protoScala classes, so `case e: X` is the same
per-class marker test as `case p: Point` and a class of your own takes its place
in the tree by extending one of them.

```text
Throwable(message)
├── Exception
│   ├── RuntimeException
│   │   ├── ArithmeticException
│   │   ├── ClassCastException
│   │   ├── IllegalArgumentException
│   │   │   └── NumberFormatException
│   │   ├── IllegalStateException
│   │   ├── IndexOutOfBoundsException
│   │   │   └── StringIndexOutOfBoundsException
│   │   ├── MatchError
│   │   ├── NoSuchElementException
│   │   ├── NullPointerException
│   │   ├── UninitializedFieldError
│   │   └── UnsupportedOperationException
│   ├── InterruptedException
│   └── IOException                        (Track F)
│       ├── FileNotFoundException
│       └── CharacterCodingException
│           └── MalformedInputException
└── Error
    ├── AssertionError                     (Track X)
    ├── NoSuchMethodError
    ├── NotImplementedError                (Track X)
    ├── OutOfMemoryError
    └── StackOverflowError
```

`Throwable` answers `getMessage`, `getCause` (always `null`), `getClass` (the
simple name as a `String` — D86) and a `toString` of `<class>: <message>`.
`UninitializedFieldError` extends `RuntimeException` despite its name, as
`scala.UninitializedFieldError` does. `IOException` sits under `Exception` and
**not** under `RuntimeException`, as on the JVM, so `catch case e: IOException`
sees every file failure and nothing else (D97). `AssertionError` and
`NotImplementedError` sit under `Error`, as `java.lang.AssertionError` and
`scala.NotImplementedError` do, so `catch case e: Exception` does **not** swallow
a failed `assert` or a `???` (§4.4). A defect in protoScala itself is **not** in
this tree and is not catchable (D74).

### 3.2 Imports, and modules (Phase 6, corrected by Track X)

`import` has **two** meanings here, and until Track X this section documented
only one of them. Plain Scala's **member import** brings names out of something
already in scope:

```scala
enum Color:
  case Red, Green
import Color.*
println(Red)          // Red
```

and Phase 6 added a second form, the **module import**, which loads a file:

```scala
import util.Strings.shout     // loads util/Strings.scala
```

**How they are told apart.** The longest dotted prefix of the path that names
something **already in scope** wins, and the import reads its members; if no
prefix does, the path is a module to load, exactly as in Phase 6. A family
prefix (`py.`, `js.`, `st.`, `clj.`) still wins over both. Scala's own rule has
the same shape — a definition in scope shadows a package of that name — so a file
that defines `object util` and writes `import util.Shapes` gets its own object in
Scala too, and gets it here.

"In scope" is deliberately narrow: a term whose **class the compiler knows**,
which is an `object`, a companion object, or the companion an `enum` desugars to.
A wildcard has to enumerate the prefix's members, and a `val` has no static type
to enumerate (D4), so `import someVal.*` is **not** a member import and falls
through to the module loader and its `ImportError`. A prefix that is neither gets
Phase 6's message, naming every path it tried.

| Form, with `Obj` an object / companion / enum in scope | Binds |
|---|---|
| `import Obj.*` / `._` | every member of `Obj`, plus every `enum` case and nested template of `Obj` under its simple name |
| `import Obj.{a, b}` | `a` and `b`, each rewriting to the member access `Obj.a` compiles to |
| `import Obj.a as b` / `.{a => b}` | `b` only; `a` is not bound |
| `import Obj.Inner` | the **type** `Inner` and its companion term, so `new Inner(…)`, `case i: Inner` and `case Inner(x)` all compile |
| `import Obj as O` | `O` as another name for the object |
| `import Obj.given` | parsed and **ignored** (D3, D93) |

Two details this form turns on. An `enum`'s cases and a template nested in an
object are lifted to top-level definitions with **dotted** names (`Color.Red`), so
they are not members of the companion; a wildcard that only walked the companion's
members would bind nothing at all, which is why `import Color.*` reads both. And
the prefix is bound internally under a name no user can write, pinned to the key
it had when the import was taken, so a later REPL redefinition of the prefix
cannot redirect an import made before it (D25).

The rest of this section is the **module** half.

A **module** is a `.scala` file reached by its path. `util/Shapes.scala` is the
module `util.Shapes`; the file is desugared into a synthetic `object Shapes`, so
its classes become `Shapes.Point` with their companions and its `def`s become
members (**D91**). The module's name comes from the *file*, not from anything
written inside it, and a module may not define an `@main` — it is imported, not
run, and a silently ignored `@main` would be a trap.

A module's top level runs **when it is imported** (**D90**), once per canonical
absolute path. Scala has no file-level modules, so there is nothing to diverge
from; Python and JavaScript both run a module at import, and a module that exists
for its effects would otherwise never run. A cycle is refused
(`cyclic module import: <path>`), and a failed load is not cached, so a fixed file
can be imported again in the same session.

| Form, with `util.Strings` a **file** and nothing of that name in scope | Binds |
|---|---|
| `import util.Strings` | `Strings` → the module object |
| `import util.Strings as S` | `S` → the same |
| `import util.Strings.{trim, pad as p}` | `trim` and `p`, each rewriting to the member access the qualified spelling compiles to |
| `import util.Shapes.{Point}` | the **type** `Point` and its companion term, so `new Point(1, 2)`, `case p: Point` and `case Point(x, y)` all compile |
| `import util.Strings.*` / `._` | every exported member and every nested type under its simple name |
| `import util.Strings.given` / `.{given T}` | parsed and **ignored** (D3, D93) |
| `import py.numpy as np` | the `py` provider's module, as a `Val` with no types (D94) |
| `import py.numpy.*` | **refused** (D92): a foreign object's names cannot be enumerated |

An import is **resolved when the file is compiled**, which is why an imported
class is usable as a type. `import a.b.C` tries the longest dotted prefix first —
module `a.b.C`, then module `a.b` with member `C` — and a miss names every path
it tried. Modules are searched in the importing file's directory, then each
colon-separated entry of `PROTOSCALA_PATH`, then the working directory.

An import is **hoisted to its compilation unit** (**D96**): a top-level import is
visible for the whole unit, and one written inside a block or a template body
takes effect where it appears and outlives that block. Scala scopes an import
lexically; matching that needs a scope-aware resolver this compiler does not have,
and D82's extensions have the same shape, so the two are scoped together or not at
all.

An imported member is consulted **after** locals and members and **before**
globals: an inner scope wins over an import, and an import shadows an outer
binding, both as in Scala. A name this unit declares *and* an import binds is a
compile error rather than a silent shadow.

See [INTEROP.md](INTEROP.md) for the polyglot half and what it can reach today.

## 4. Standard library (Phases 3–5)

Phase 2 shipped a subset ahead of Phase 3, because for-comprehensions and
extractors needed it: `Option`/`Some`/`None` from the embedded prelude
(`lib/prelude.scala`), and `List(...)`, `Nil`, `::`, `map`, `flatMap`,
`filter`, `withFilter`, `foreach`, `length`, `tail`, `drop`, `mkString`.
**Phase 3 completed the rest**; §4.2 is the surface as delivered.

Delivered:

- `Any`, `AnyRef`, `Nothing`, `Unit`, `Int`, `Long`, `Double`, `Boolean`,
  `Char`, `String`, `BigInt` (all dynamic, DESIGN §4.1).
- `List` (`Nil`, `::`), `Vector`, `Map`, `Set`, `Range`,
  `Option`/`Some`/`None`, `Either`/`Left`/`Right`,
  `Try`/`Success`/`Failure`, `TupleN`.
- The collection methods of §4.2.
- `println`, `print`, `scala.math` basics.
- `Actor`, `Future`, `Priority`, `Thread`, `System` (Phase 5, §4.1).
- The Predef surface of §4.4: `assert`, `assume`, `require`, `???` and `App`
  (Track X).

Still ahead: `sys.exit`, `identity`, `locally`, `StringBuilder`, `Symbol`,
`Enumeration`, `scala.util.control.Breaks`, and the `scala.math` surface beyond
the basics. That list is not a guess: it is what the Scala 3 run-corpus
measurement (STATUS.md, "Known issues") found the corpus asking for, in the order
of how often it asked.

**Not provided, and not scheduled.** `Seq` and `Iterable` are **not** traits of
this dialect and no phase's done-when contains them: `case xs: Seq[_]` and
`x.isInstanceOf[Seq[_]]` are rejected at compile time (D65), and `List`,
`Vector`, `Range`, `Map` and `Set` share no ancestor below `AnyRef` — although
they *do* compare and hash as Scala `Seq`s do, so `List(1,2) == Vector(1,2) ==
(1 to 2)`. `collect` is not provided either (D63): a `PartialFunction` needs the
compiler to emit a second entry point per `{ case … }` literal, and Phase 4's
pattern-matched `catch` reuses `compileMatch`'s cascade instead of introducing one,
so this is still open; write `xs.filter(p).map(f)`. There is no
`Ordering` (D62), no `SortedMap`/`ListMap`, no `Array` (D69) and no regular
expressions (D70).

### 4.2 The collection surface as delivered (Phase 3)

Every method below is implemented and covered by a conformance fixture. A method
that is not listed does not exist and fails with `NoSuchMethodError`.

- **`List` and `Vector`** (one implementation, installed on both, so the two
  cannot drift; a result is of the receiver's own kind): `length`, `size`,
  `isEmpty`, `nonEmpty`, `apply`, `head`, `last`, `headOption`, `lastOption`,
  `tail`, `init`, `take`, `drop`, `takeWhile`, `dropWhile`, `splitAt`,
  `reverse`, `++`, `:+`, `+:`, `updated`, `contains`, `indexOf`, `exists`,
  `forall`, `count`, `find`, `partition`, `map`, `flatMap`, `filter`,
  `filterNot`, `withFilter`, `foreach`, `zip`, `zipWithIndex`, `distinct`,
  `flatten`, `toList`, `toSeq`, `toVector`, `toSet`, `toMap`, `groupBy`,
  `iterator`, `foldLeft`, `foldRight`, `fold`, `reduce`, `reduceLeft`,
  `reduceRight`, `sum`, `product`, `min`, `max`, `minBy`, `maxBy`, `sorted`,
  `sortBy`, `sortWith`, `mkString` (0, 1 or 3 arguments). `List` also has `::`;
  prepending to a `Vector` is `+:`. `foldLeft`/`foldRight` accept both the Scala
  spelling `xs.foldLeft(z)(f)` and the one-list `xs.foldLeft(z, f)`.
- **`Range`** (`0 until 5`, `1 to 5`, `… by step`): `length`, `size`,
  `isEmpty`, `nonEmpty`, `apply`, `head`, `last`, `contains`, `sum`, `by`,
  `map`, `flatMap`, `filter`, `withFilter`, `foreach`, `exists`, `forall`,
  `count`, `find`, `reverse`, `toList`, `toSeq`, `toVector`, `toSet`,
  `mkString`, `toString`. `length`, `apply`, `head`, `last` and `sum` are O(1)
  arithmetic: a `Range` is never materialised except by `toList`, `toVector`,
  `toSet` and `mkString`.
- **`Map`**: `apply`, `get`, `getOrElse`, `contains`, `isDefinedAt`, `size`,
  `length`, `isEmpty`, `nonEmpty`, `keys`, `keySet`, `values`, `head`, `toList`,
  `toSeq`, `iterator`, `+`, `updated`, `-`, `removed`, `++`, `--`, `foreach`,
  `map`, `flatMap`, `filter`, `filterNot`, `withFilter`, `count`, `exists`,
  `forall`, `find`, `foldLeft`, `toMap`, `toSet`, `mkString`, `toString`,
  `equals`, `hashCode`, `##`. A function may be written `(k, v) => …` or
  `p => p._1`; the arity of the value decides.
- **`Set`**: the same list, plus `union`/`|`, `intersect`/`&`, `diff`/`&~` and
  `subsetOf`; `s(x)` is `contains(x)`.
- **`Option`**: `isEmpty`, `isDefined`, `nonEmpty`, `get`, `getOrElse`,
  `orElse`, `map`, `flatMap`, `filter`, `withFilter`, `foreach`, `contains`,
  `exists`, `forall`, `count`, `fold`, `zip`, `toRight`, `toLeft`, `orNull`,
  `toList`, `toSeq`, `iterator`.
- **`Either`** (right-biased): `isLeft`, `isRight`, `map`, `flatMap`,
  `foreach`, `getOrElse`, `fold`, `swap`, `toOption`, `toList`, `exists`,
  `forall`, `contains`. No `withFilter` (D67).
- **`Try`**: `isSuccess`, `isFailure`, `get`, `getOrElse`, `toOption`, `map`,
  `flatMap`, `foreach`, `recover`, `recoverWith`, `orElse`, `toEither`.
  `Try { … }` takes its body by name. No `filter`/`withFilter` (D67).
- **`String`**: `length`, `isEmpty`, `nonEmpty`, `charAt`, `apply`,
  `substring`, `toUpperCase`, `toLowerCase`, `trim`, `contains`, `startsWith`,
  `endsWith`, `indexOf`, `lastIndexOf`, `reverse`, `*`, `+`, `concat`,
  `toInt`, `toLong`, `toDouble`, `toBoolean`, `split`, `replace`,
  `stripMargin`, `stripPrefix`, `stripSuffix`, `capitalize`, `repeat`,
  `compareTo`, `equalsIgnoreCase`, `format`, `toList`, `head`, `last`, `init`,
  `take`, `drop`, `takeWhile`, `dropWhile`, `map`, `filter`, `foreach`,
  `mkString`.
- **`Any`**: `->`, so `k -> v` is the `Tuple2` `(k, v)`.

### 4.1 Concurrency (Phase 5)

Actors and futures are ordinary values with ordinary methods; the dialect adds
no syntax and no opcode for them.

```
ActorExpr    ::= 'Actor' '.' 'spawn' '(' Expr ')' '(' Expr ')'   -- two argument lists
Tell         ::= Expr '!' Expr                                   -- infix, returns Unit
Ask          ::= Expr '?' Expr                                   -- infix, returns Future
```

| Receiver | Member | Meaning |
|---|---|---|
| `Actor` | `spawn(state)(handler)` | a new actor; `handler(state, msg)` returns `(newState, reply)`, or a bare `newState` when there is no reply (D45) |
| `Actor` | `isActor(x)`, `stats` | a predicate; `ActorStats(workers, messagesProcessed)` |
| `Priority` | `High`, `Medium`, `Low` | a prelude `enum` whose ordinals are `0`, `1`, `2`; a plain `Int` band is also accepted |
| an actor | `! msg` | tell, on the Medium band; `Unit` |
| an actor | `? msg` | ask, on the Medium band; a `Future` of the reply |
| an actor | `send(msg, p)`, `ask(msg, p)` | the same on an explicit band |
| an actor | `value` | the current state, read without a message (D51) |
| a future | `await` | the value; raises the error of a failed future |
| a future | `isCompleted`, `value` | `Boolean`; `Option[Try[T]]` (`None` while pending) |
| a future | `map`, `flatMap`, `recover`, `onComplete` | combinators; the continuation runs on the completing thread (D48) |
| `Future` | `apply(e)`, `successful(v)`, `failed(e)` | `apply` takes its body **by name** and runs it on the worker pool (D47) |
| `Thread` | `start(() => e)`; a thread's `join()` | a real OS thread in the collector's quorum (D49) |
| `System` | `nanoTime()`, `currentTimeMillis()`, `getenv(name)` | `Long`, `Long`, `String` (D49) |

`await` inside an actor handler suspends the handler cooperatively: the worker
is released, the actor stays claimed and its queued messages wait until the
handler finishes (DESIGN §8.3). Outside an actor it blocks the calling thread.
The chains it can suspend are limited by D43.

The prelude gains `Try`/`Success`/`Failure` ahead of Phase 3; since Phase 4
`Failure` carries the `Throwable` itself, so an `await` on a failed future raises
that value at its own call site and an enclosing `try` catches it.

### 4.3 Files (Track F)

**Reading** is `scala.io.Source`, under Scala's own names. There is no `scala.io`
namespace to hold it — there are no packages (§3.2) — so `Source` is a prelude
global, like `List` and `Try`.

```text
Source.fromFile(path: String, enc: String = "UTF-8"): BufferedSource
Source.fromString(s: String): BufferedSource

BufferedSource.mkString: String            the whole file, terminators included
BufferedSource.getLines(): List[String]    the lines, terminators stripped (D100)
BufferedSource.close(): Unit
BufferedSource.isOpen: Boolean
```

`getLines()` breaks on `\n`, `\r\n` and a lone `\r`, strips the terminator, adds
no final empty line for a file that ends in one, and answers no lines at all for
an empty file — all of it as Scala 3 does, verified against scalac 3.9.0. It
answers a `List[String]` and not an `Iterator[String]`, because there is no
`Iterator` (D100), and a source may be read more than once, because the file is
read when it is opened (D101). Only UTF-8 is decoded, and strictly (D99).

**Writing** is protoScala's own surface and deliberately **not** a simulated
`java.io.PrintWriter`: there is no Java interop to build one on (D8), and
imitating one would mean inventing a `Writer`, a stream hierarchy, a `flush` and a
buffering policy. This is the deviation the absence of Java causes, and it has its
own id, **D102**.

```text
FileIO.write(path: String, text: String): Unit     replace the contents, creating the file
FileIO.append(path: String, text: String): Unit    add to the end, creating the file
FileIO.exists(path: String): Boolean               is anything at this path
FileIO.delete(path: String): Boolean               true if removed, false if there was nothing
```

The text is written as UTF-8, which is what `Source.fromFile` reads back. Neither
`write` nor `append` creates parent directories. `delete` removes a file and
**raises** rather than answering `false` for any failure other than "there was
nothing there" — including a directory, which it refuses.

Every failure raises a class from §3.1 with a message naming the path: a failure
of `open` is a `FileNotFoundException` whatever its cause, a failure after that is
an `IOException`, invalid UTF-8 is a `MalformedInputException`, and a path holding
a NUL byte is an `IllegalArgumentException` rather than a silent read of a
different file (D97, D98).

What is absent: directories, listing, rename, binary files, random access, stdin,
streaming of any kind (a file is read whole), and everything in `java.io` and
`java.nio`.

### 4.4 The Predef surface (Track X)

Scala puts `assert`, `require`, `???` and friends in `scala.Predef`, which every
file sees without importing it. There are no packages here (§3.2), so they are
prelude globals, like `List` and `Try`. Every exception type and every message
text below was verified against **scalac 3.9.0** and matches it exactly.

```text
assert(cond: Boolean, message: => Any = <none>): Unit
    cond false  ->  AssertionError("assertion failed")
                    AssertionError("assertion failed: " + message)
assume(cond: Boolean, message: => Any = <none>): Unit
    cond false  ->  AssertionError("assumption failed")
                    AssertionError("assumption failed: " + message)
require(cond: Boolean, message: => Any = <none>): Unit
    cond false  ->  IllegalArgumentException("requirement failed")
                    IllegalArgumentException("requirement failed: " + message)
???: Nothing   ->  NotImplementedError("an implementation is missing")
```

Three details a program can depend on:

- **A call with no message gets the bare prefix.** `assert(false)` says
  `assertion failed`, not `assertion failed: assertion failed`. A message of
  `null` is a message, and is reported as `null`.
- **`AssertionError` and `NotImplementedError` extend `Error`, not `Exception`**
  (§3.1), so `catch case e: Exception` does not swallow a failed assertion. A
  failed `require` is an `IllegalArgumentException` — a `RuntimeException` — because
  it blames the caller's argument, not the code that checked it.
- **The message is by-name**, so an assertion that holds never builds it.

`assert` and `assume` are *macros* in Scala, which `-Xdisable-assertions` can
remove from the bytecode; these are methods, and nothing elides them (**D103**).

`object Main extends App` runs the object's body as the program (**D104**), as in
Scala. It is deprecated in Scala 3 in favour of `@main`, which protoScala also
supports, and it is kept because it is what most Scala teaching material writes.
protoScala adds three restrictions Scala does not have: one App object per file,
not an App object *and* an `@main` in the same file, and no App object in a
module — each a compile error naming both candidates, because a script has no
class name with which to choose an entry point and a silently ignored one is a
trap.

## 5. Departures from Scala (stable ids; mirrored in STATUS.md)

| Id | Departure | Reason |
|---|---|---|
| D1 | `Int`/`Long` never overflow: results promote to arbitrary precision; integer literals have no range limit (`0xFFFFFFFF` is `4294967295`, `2147483648` needs no `L`) | protoCore integer model |
| D2 | `Float` is `Double` | protoCore has one floating type |
| D3 | No implicits / givens / `using` resolution (parsed, rejected with a clear error if used) | resolution needs static types |
| D4 | No exhaustiveness or static type checking; type errors surface at run time | types are erased |
| D5 | Access modifiers advisory except `private`: a private member is stored under a class-qualified key, reachable only from code of its class and companion; an access from elsewhere fails at run time with `NoSuchMethodError`; `protected`, `override` and member `final` are not checked. The "weaker access privileges" check that rejects a `private` member implementing an abstract one applies only to a `private` the user wrote, never to a synthesised member | no static checker |
| D6 | Extension methods dispatch on the runtime prototype, not the static type | types are erased |
| D7 | `Map`/`Set` iteration order is unspecified and may differ from Scala's | persistent structures; no ordering guarantee beyond Scala's own (`ListMap`/`SortedMap` are separate types, later) |
| D8 | Java interop (`java.*` classes) is absent; polyglot interop goes through UMD | no JVM |
| D10 | A bare reference to `def f()` (empty parameter list) evaluates to the function (Scala 3 requires `f()` unless a function type is expected). Also: `obj.m` for a method **with** parameters is a function value (eta-expansion), and `obj.m()` on a parameterless `def m` whose result is a function applies that result (`obj.m.apply()`), as Scala does | no static expected types |
| D26 | Value discarding (`Unit` expected type) applies only where `Unit` is written on the definition or an ascription, not when it comes from a function type (`val f: Int => Unit = x => x + 1` returns `x + 1`) | types are erased |
| D27 | `@main` methods take no parameters or one `String*` parameter; typed `@main` parameters are rejected | no `FromString` instances without static types |
| D28 | Instances of classes without `var` fields are immutable and rebuilt field by field during construction: every field store returns a *new* object and `new` yields the last one. A reference to `this` that escapes before the last field is initialised — stored in a field, passed to another object, or **captured by a closure created in the constructor, including the initialiser of a `val` member** — therefore denotes an earlier version of the object: it lacks every field stored after that point (reading one raises `NoSuchMethodError: value <f> is not a member of <C>`) and it is neither `==` nor `eq` to the finished instance. A class that declares a `var`, itself or through an ancestor, builds mutable instances, where stores happen in place and neither problem arises. Scala has neither restriction | immutability by default (DESIGN §4.2) |
| D29 | Type tests follow the runtime representation: `Int`, `Long`, `Short`, `Byte` and `BigInt` are one integer type (D1), `Float` is `Double` (D2), so `3L.isInstanceOf[Int]` is `true`; `asInstanceOf` never converts numbers (`(1: Any).asInstanceOf[Double]` throws); `null.asInstanceOf[Int]` is `null`; an extractor's `unapply` is called without the type test its parameter type implies | D1, D2, D4 |
| D30 | Reading a field of the instance under construction before its initialiser has run raises `NoSuchMethodError` (Scala reads the default value `0`/`null`) | no declared field defaults |
| D31 | Methods cannot be overloaded **in a template** (a second definition of a name there is an error); constructors may be overloaded by number of parameters only. Top-level `def`s *are* overloadable by number of parameters (D111) | types are erased |
| D32 | Tuples have at most 22 elements (Scala 3 has `TupleXXL`) | `Tuple2`..`Tuple22` are case classes |
| D33 | `Option.getOrElse` evaluates its default eagerly: it is a method reached through a dynamic send, where a by-name parameter is not honoured (D53) | D53 |
| D34 | A pattern-matching function literal `{ case ... }` takes exactly one argument (Scala 3 also accepts it where a function of several parameters is expected) | no expected function type |
| D35 | With `case`, a generator filters exactly as Scala 3 does, for every pattern. **Without** `case`, a refutable pattern generator — `for (Some(v) <- os)` — is accepted and also filters, where scalac 3.9 rejects it ("pattern's type `Some[Int]` is more specialized than the right hand side expression's type `Option[Int]`") and asks for `case`. The one pattern the no-`case` form does not filter is a tuple pattern, which is trusted to meet tuples (types are erased): `for ((a, b) <- List(1, (1, 2)))` raises `MatchError` on the `1`. scalac rejects that program too, so there is no runtime answer to diverge from | no static types to detect the narrowing |
| D36 | For-comprehension desugaring differs where the result does not: `for (x <- xs; y = e)` emits two `map`s instead of dotty's single fused `map`. It produces the values scalac produces; only the number of intermediate traversals differs | simpler desugaring |
| D37 | An intersection type cannot be tested at run time: `case v: (A & B)` and `x.isInstanceOf[A & B]` are rejected at compile time with "this type cannot be tested at run time", where scalac 3.9 accepts them and tests both components. (The unparenthesised `case v: A & B` is a syntax error in scalac and is rejected here too, with a different message.) A parenthesised *tuple* type, `case v: (Int, Int)`, is accepted here and by scalac, and both test only the tuple's erasure | one marker attribute per class |
| D38 | The default `toString` of an object with no user-written `toString` is `Name@<identity hash>`: a singleton `object O` prints `O@…` where the JVM prints `O$@…`, and printing a companion or a tuple companion directly (`println(Tuple2)`) prints `Tuple2@<hash>` | no JVM name mangling |
| D39 | `List` hash codes differ from the JVM's, while staying consistent with `==` (equal lists have equal hash codes). Case classes, case objects, tuples and strings hash bit-identically to the JVM | `List` is a `ProtoList` |
| D40 | Diagnostic wording: a `var` that redefines a concrete inherited `var` without `override` is reported as "cannot override a mutable variable", where scalac says it "needs `override` modifier". protoScala rejects `override` on a `var` outright, so the two messages describe the same rejected program from opposite ends | `override` is never accepted on a `var` |
| D41 | Scala 3's universal apply (a creator application: `C(args)` standing for `new C(args)`) is **not** synthesised for a plain class. `class C(val a: Int); C(1)` fails with `Not found: C`, where scalac 3.9 compiles it and prints `1`. Write `new C(1)`, or give `C` a companion with an `apply`. Case classes and case objects are unaffected: their companion `apply` is synthesised, so `C(args)` works | no static expected type to place the creator application |
| D42 | A `MatchError` names the Scala class of the unmatched value: `MatchError: 5 (of class Int)`, where the JVM names the boxed class (`scala.MatchError: 5 (of class java.lang.Integer)`). Consistent with D14 (unqualified class names) and D29 (one integer type) | types are erased; there is no boxed class to name |
| D43 | `await` suspends only a chain of protoScala frames each stopped at a call instruction. Inside a native higher-order method (`map`, `foreach`, `withFilter`, a `Future` continuation), or under an instruction that calls back into Scala without being a call site, it raises `UnsupportedOperationException` | a recursive VM cannot snapshot a C++ frame |
| D45 | An actor handler returns **either `(newState, reply)` or a bare `newState`**; the bare form means there is no reply to give and the ask's future completes with `()`. A `Tuple2` result is always read as the pair form, so an actor whose state is itself a pair returns it inside one. Only a handler that produces no value at all is rejected, with `IllegalArgumentException` | least surprise: a handler that only updates state should not have to invent a reply |
| D46 | An actor lives as long as the session (it is anchored so the collector can reach it while only the C++ ready stacks refer to it); `Future.apply` creates one actor per call | a removable registry needs `ProtoMap` |
| D47 | `Future.apply` takes its body **by name**: `Future(expr)` and `Future { … }`, as in Scala. By-name parameters are honoured where the compiler can name the callee (D53) | least surprise: Scala's `Future` takes a by-name body |
| D48 | `map`/`flatMap`/`recover`/`onComplete` run their continuation on the thread that completes the future, or on the caller when it is already complete; there is no `ExecutionContext`, and a continuation may not `await` | one scheduling entity |
| D49 | `Thread` and `System` are runtime facilities, not the JVM's; `nanoTime` is monotonic, only differences are meaningful | no JVM |
| D51 | `actor.value` reads the state without sending a message, so it may observe a state older than a queued send | a lock-free read |
| D53 | A by-name parameter is honoured only where the compiler resolves the call site to the declaration: a call by name to a top-level or local `def` (any parameter list, including a curried one), a method of the template being compiled, a method of an `object`, a class's primary constructor, and a builtin whose by-name signature the runtime declares (`Future.apply`). At any other call site — a method reached through a dynamic send, or a `def` taken as a function value — the argument is evaluated once at the call and each read of the parameter yields that value. scalac resolves all of these statically, so it stays lazy where protoScala does not | dynamic dispatch: a send carries no signature |

| D54 | `s"…"` and `raw"…"` compile directly to a concat opcode, so a user-defined `StringContext` is never consulted. The same input produces the same string | one rope join per interpolation instead of three allocations and two sends |
| D55 | The `f` interpolator supports `%s %b %c %d %o %x %X %e %E %f %g %G %%`, the flags `-`, `+`, space, `0`, `,` and `#`, and `width.precision`. `%n` is not supported — write `\n` — and there is no locale, so the decimal separator is always `.` | a locale means a locale database |
| D56 | **Retired by Phase 4.** Any interpolator other than `s`, `f` or `raw` is lowered to `StringContext(<literals>).<name>(<args>)` and supplied as an extension method on `StringContext`, as in Scala; an undefined one is a run-time `NoSuchMethodError` | delivered |
| D58 | `Map`/`Set` iteration — `toString`, `foreach`, `keys`, `values`, `toList`, `mkString` — is in ascending-hash order: deterministic for a given key set, unrelated to insertion order or to Scala's. The concrete consequence of D7 | Scala guarantees no order either |
| D59 | `Vector.hashCode` equals `List.hashCode` for the same elements, and a `Range`'s equals both; all three differ from the JVM's (D39) | required for `Map(List(1) -> 1)(Vector(1))` to work |
| D61 | `to`, `until` and `by` require bounds that fit a 54-bit integer | matching means boxing every bound, an allocation on every `Range` method |
| D62 | `sorted` orders numbers, `Char`s, strings and booleans with the runtime's own comparison; any other pair raises `IllegalArgumentException: sorted needs comparable elements; use sortWith`. `sorted`, `sortBy` and `sortWith` are stable, as Scala's are | there is no `Ordering` without implicits (D3) |
| D63 | `collect` is not provided; write `xs.filter(p).map(f)` | a `PartialFunction` needs a second entry point per `{ case … }` literal; Phase 4's pattern-matched `catch` reuses `compileMatch` instead, so this is still open |
| D65 | `Seq` and `Iterable` are not provided: `case xs: Seq[_]` is rejected at compile time, and the collections share no ancestor below `AnyRef` — although they compare and hash as Scala `Seq`s do | no phase's done-when contains them |
| D66 | `%e`, `%f` and `%g` convert through a `Double`, so an integer above 2^53 prints rounded; `%d` is exact | matching means a bignum decimal formatter |
| D67 | Neither `Either` nor `Try` has `filter`/`withFilter`, so `for (x <- e if p)` over one raises `NoSuchMethodError` | Scala's `Either.withFilter` needs a `Left` only the static type supplies; `Try`'s needs an exception value, which Phase 4 now has, so that half is merely unimplemented |
| D68 | `Range.map`, `flatMap` and `filter` answer a `List` where Scala answers an `IndexedSeq`; the elements and their order are Scala's. `Range.reverse` does answer a `Range` | `IndexedSeq` is a `Seq` trait (D65) |
| D69 | `String.split` answers a `List[String]`, not an `Array[String]` | there is no `Array` type (D12) |
| D70 | `String.split` takes a **literal** separator: `"a.b".split(".")` yields `List(a, b)` where Scala, reading a regex, yields `List()`. `split(",")` is identical | matching means a regex engine |
| D71 | A key that overrides `equals` but not `hashCode` misses at once. Scala's `Map1`..`Map4` compare by `==` alone and hide the classic defect up to four entries; at five and beyond Scala answers the same as protoScala | matching means a second, unhashed small-map representation |

| D72 | A `finally` body that itself throws replaces the in-flight exception. Scala does the same and warns; protoScala has no warnings (D4) | the cleanup is not protected by its own handler entry |
| D73 | Exception class names are unqualified and the hierarchy is the twenty-class one of §3.1; `catch { case e: java.io.IOException => }` does not compile | there is no `java` namespace (D8) |
| D74 | A `std::logic_error` from a compiler or VM defect is not translated and **not catchable**: it reaches the outermost handler as `protoscala: internal error: …` | a bug in the language must never be masked by a program written in it |
| D75 | An actor suspended on a future that never completes never runs the `finally` of the `try` it suspended inside; the shutdown diagnostic reports how many are parked | a suspension is not an abandonment; the alternative is running user code during shutdown |
| D76 | `super[T].m` accepts any ancestor in the receiver's linearization, where scalac requires `T` to be a **direct** parent | `ClassInfo` stores the flattened linearization and no direct-parent list |
| D77 | `enum` `values` answers a `List`, where Scala answers an `Array`; the elements and their order are identical | there is no `Array` type (D69) |
| D78 | **Does not exist** and is not reused: it was reserved for an `enum` case resolving without its qualifier, and matching Scala — which requires `E.Case` — cost nothing | — |
| D79 | A `derives` clause on an `enum` is parsed and ignored, as on every other template (D3); `enum` type parameters are erased, and a case may not override a member of the enum class | there are no type classes to derive |
| D80 | A `class`, `trait` or `object` nested in a **`class`** or **`trait`**, a local class in a block, and `new T { … }` are rejected; nesting in an **`object`** is supported | each captures the enclosing instance, which needs a per-instance class |
| D81 | A named argument is bound in the **callee**, so a typo in a parameter name, an argument given twice and one left unfilled are all run-time `IllegalArgumentException`s naming the method and the parameter, where scalac rejects them at compile time | the platform is late-binding even where Scala is not; that is what makes a named argument work at an unresolvable call site |
| D82 | An extension is global and session-wide, with no import scoping | the import mechanism arrived in Phase 6 and scoping was **considered and declined**: protoScala hoists an import to its unit (D96), so "scoped" would mean per-*unit* — a third behaviour that is neither Scala's per-block scoping nor today's session-wide visibility. The two are scoped together when a scope-aware resolver exists, or not at all |
| D83 | An extension on a builtin type mutates that prototype for the session, and a collision with an existing member of the type is refused where scalac allows the shadowing | silently shadowing a builtin method would be unrecoverable within a session |
| D84 | `class C(a: Int)(b: Int)` has one flat parameter list, so `new C(1)(2)` and `new C(1, 2)` are the same call and a constructor cannot be partially applied | one positional parameter list per callable |
| D85 | `catch someFunction` is accepted and rewritten to `case e => someFunction(e)`; a genuine `PartialFunction` rethrows in Scala and raises `MatchError` here | permissive and cheap; rejecting it later would break programs |
| D86 | `Throwable.getClass` answers the class's simple name as a `String` | there are no `Class[_]` values |
| D87 | The cleanup a `return` inlines is excluded from its own `try`'s handler range; the multi-level variant (an inner handler seeing an outer cleanup's exception during a `return`) is not implemented | matching exactly needs a per-construct hole stack keyed by nesting depth |
| D88 | A default value may read a parameter of the **same** list, which scalac requires to come from a previous one; the curried spelling works here too | Desugar has already folded the extra lists into lambdas |
| D89 | A named argument on a function value binds against the names in the function literal, where scalac rejects it (`Function2.apply` names its parameters `v1`, `v2`) | binding in the callee makes the literal itself the callee |

D9–D25 are the provisional Phase 1 departures, D28–D42 the provisional
Phase 2 departures, D43–D53 the provisional Phase 5 departures, D54–D71 the
provisional Phase 3 departures and D72–D89 the provisional Phase 4 departures
listed in [STATUS.md](STATUS.md#intentional-deviations). The maintainer reviewed
the first three groups on 2026-09-23: approved as recorded, except D45 and D47,
which were overturned (the rows above carry the replacement behaviour) and which
brought D53 with them. The Phase 3 and Phase 4 groups are **pending review**.

**D44, D50 and D52 no longer exist.** They were provisional Phase 5 behaviours
that Phase 4 retired: a failed `Future` carried a `RuntimeError` case class rather
than a `Throwable`, awaiting a failed future abandoned the rest of the handler,
and `Priority` was three integers on an object. Their ids are not reused.

| D90 | A module's top level runs **when it is imported**, during the importing unit's compilation, not lazily on first member access | Scala has no file-level modules; Python and JavaScript both run a module at import, and a module imported for its effects would otherwise never run |
| D91 | A module **is an `object`** named after its **file**; its classes are `M.C` with their companions; it may not define an `@main`. An `import` and an `extension` written in a module stay outside that object | Phase 4's nested-template lifting already gives qualified names, companions and sibling resolution, so a bespoke module object model would be a second object model for what the first expresses |
| D92 | A **wildcard import of a foreign module** (`import py.numpy.*`) is refused; named selectors work | a foreign object's attribute names cannot be enumerated, and a guessed name set would fail silently later. The message names the working spelling |
| D93 | `given` selectors are parsed and **ignored**, as every other given is (D3) | there are no type classes to resolve |
| D94 | A **foreign module binds no types**: `new`, a type pattern and `isInstanceOf` are unavailable on a class reached through a family prefix; its members resolve by name at run time | a foreign value carries no `ClassInfo`, which is what DESIGN §5's type-mapping table already says |
| D95 | `--disassemble` **resolves imports**, and therefore runs the top level of every module the file imports | a file cannot be compiled without its imports, and an import is resolved by loading (D90) |
| D96 | An `import` is **hoisted to its compilation unit** rather than scoped lexically: a top-level import is visible for the whole unit, and one inside a block outlives that block | lexical scoping needs a scope-aware name resolver the compiler does not have, and D82's extensions have the same shape, so the two are scoped together or not at all |
| D97 | The file-I/O exceptions are the JVM's **without the `java.io.` / `java.nio.charset.` prefixes**, in the JVM's shape, with `IOException` under `Exception` and not `RuntimeException`. A failure of `open` is a `FileNotFoundException` whatever its errno; a failure after it is an `IOException` | no `java` namespace (D8); a Scala programmer's `catch case e: IOException` must see a missing file and a bad byte alike. The class of each failure was verified against scalac 3.9.0 |
| D98 | A failure message keeps the JVM's `<path> (<reason>)` shape with the reason in **English** from an errno table, not from the localised `strerror`. `MalformedInputException` names the path and the byte offset where Scala's says only `Input length = 1` | a fixture cannot pin a locale-dependent message, and a message that does not name the file is useless once several were read |
| D99 | **UTF-8 only.** `Source.fromFile(path, enc)` accepts the argument and refuses any name but UTF-8 with an `UnsupportedOperationException`. Decoding is strict: overlong forms, surrogates, values above U+10FFFF and truncated sequences are all `MalformedInputException` | a charset table is real machinery; decoding Latin-1 as UTF-8 would hand the program plausible nonsense, so it is refused instead. No implicit `Codec` (D3) |
| D100 | **`getLines()` answers a `List[String]`**, not an `Iterator[String]`; a `Source` is not an `Iterator[Char]`, and nothing streams | there is no `Iterator` in this dialect. `getLines().toList` still works, since `toList` on a `List` is the identity |
| D101 | **A `Source` may be read again**; Scala's is consumed as it is read, and answers `List()` the second time. A **closed** source still fails to be read | matching Scala costs a cursor and a consumed-ness flag to reproduce a reliable source of bugs; strictly more programs work this way. Closing still means something, because reading a closed source is a bug worth reporting |
| D102 | **Writing is `FileIO.write` / `append` / `exists` / `delete`**, not a simulated `java.io.PrintWriter` or `java.nio.file.Files` | the absence of Java (D8) leaves nothing to imitate, and a simulated `PrintWriter` would mean a `Writer`, a stream hierarchy, a `flush` and a buffering policy for no gain |
| D103 | `assert`, `assume` and `require` are **methods, not macros**: `-Xdisable-assertions` has no analogue and an assertion always costs a call. Each is one method with a default message rather than Scala's two overloads | no macros and no `inline`; no overloading (D31). The default is a distinguished object, not `null`, so `assert(false, null)` still reports `assertion failed: null` |
| D104 | **`App` is the entry point**, with three restrictions Scala does not have: one App object per file, not an App object and an `@main` in the same file, and none in a module | a script has no class name with which to choose between two entry points, and a silently ignored entry point is the trap D91 refused |
| D105 | An `import` is a **member import when its longest in-scope prefix names an object, a companion or an `enum`**, and a module load otherwise. Both forms are Scala-conformant; what has no Scala counterpart is the fallback to loading a file, and what is narrower than Scala is that a `val` cannot be a member-import prefix | a wildcard must enumerate the prefix's members and a dynamic value has no static type to enumerate (D4). A same-unit member import is resolved after the unit's templates are described, so a class in the same file cannot name an imported-from-its-own-object type as a **parent**; the qualified name always works |
| D108 | An **`override val` constructor parameter is invisible to an ancestor that declares the member in its own body**: `class B { val y = 10; println(this.y) }` with `class C(override val y: Int) extends B` prints 10 where scalac prints 20. The value settles on the override once the constructor chain returns, and an ancestor that declares `y` as a *parameter* sees the override (protoScala matches scalac there) | a public member's attribute key is its plain name, so the two `y`s are one slot; scalac gives each class a field and overrides the accessor. Parameter fields are guarded with `STORE_FIELD_IF_NEW`; a body `val` cannot be, because a subclass's body `val` must be able to overwrite an ancestor's |
| D109 | A **`type` alias is recorded unit-wide**, under its simple name, so two templates in one file cannot each have their own `type T` | Scala scopes a type member to its template; protoScala has no type-name scope stack, and with types erased (D4) an alias is a naming concern only |
| D110 | **Widening to `Double`/`Float` happens where the declared type is written**: a `val`, a `var`'s initialiser, a `def` result, every parameter form, a class field and an `(e: Double)` ascription all widen, as Scala does. An **assignment** to a variable declared earlier and a **type argument** do not: `var v: Double = 1; v = 2` prints `2` (Scala `2.0`) and `val l: List[Double] = List(4, 5)` prints `List(4, 5)` (Scala `List(4.0, 5.0)`) | there is no expected type (D4); the declaration site is the only place the type is available. Remembering which names were declared `Double` without a scope would widen an unrelated same-named variable, replacing one wrong answer with another |
| D111 | **Top-level `def` overloads resolve by number of parameters.** Alternatives must differ in their parameter count, and none may have a default value, a repeated or a by-name parameter, or several parameter lists; each of those is a diagnostic. A bare overloaded `f` as a value eta-expands the first alternative. Methods in a template still cannot be overloaded (D31) | arity is what survives erasure; scalac resolves by type and accepts sets protoScala cannot tell apart, so protoScala reports them rather than keeping one silently |

**D57, D60, D64 and D78 do not exist**, and are not reused. They were reserved for
a `Char`-key divergence, a `Range == List` divergence, a `Try.apply` divergence
and an unqualified-`enum`-case divergence; the rulings of 2026-09-23, the arrival
of by-name parameters and Phase 4's choice to follow Scala each removed one. Ids
are stable references, so the gaps are left open rather than closed by
renumbering.
