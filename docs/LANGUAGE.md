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

## 2. Expressions and definitions

| Feature | Phase |
|---|---|
| `val`, `var`, `lazy val`, `def` (multiple parameter lists, default and named arguments, varargs `xs: Int*`) | 1 (defaults/named: 4) |
| by-name parameters `x: => T` on a `def`, a method or a plain constructor parameter | 5 ✅ (D47, D53) |
| `if`/`then`/`else`, `while`/`do`, blocks as expressions, `return` | 1 |
| lambdas `x => e`, `(x, y) => e`, placeholder syntax `_ + 1` | 1 (placeholders: 2 ✅) |
| infix, prefix (`-x`, `!b`, `~n`) and postfix-free method application | 1 |
| string interpolation `s""`, `f""`, `raw""` | 3 ✅ |
| `for` comprehensions (generators, guards, value definitions, patterns, `yield` and `do`) | 2 ✅ |
| `match` with the patterns of DESIGN §5.3, pattern `val`s, `{ case ... }` literals | 2 ✅ |
| `try`/`catch`/`finally`, `throw` | 4 |
| `import` (selectors, renames `as`, wildcard `*`, `given` imports parsed only) | 1 (UMD prefixes: 6) |
| top-level definitions (no wrapping `object` needed), `@main` methods | 1 |
| `package` clauses (one namespace per file) | 6 |

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

Later phases:

- `enum` with simple cases and parameterised cases; `values`, `ordinal`,
  `valueOf` (Phase 4).
- Extension methods `extension (x: T) def m ...` (Phase 4, dispatched on the
  receiver's runtime prototype — D6).
- `super[T].m`, which names the ancestor explicitly instead of taking the next
  one in the linearization (Phase 4).
- Named and default arguments for Scala-defined methods (Phase 4; native
  methods, `copy` among them, already accept them).
- Multiple constructor parameter lists (Phase 4).
- Templates nested in an `object` (Phase 4, with `enum`); local classes inside
  a block and anonymous classes `new T { ... }` after that. Until then a
  `class`, `trait` or `object` must be defined at the top level of a file, and
  anything else is rejected with "must be defined at the top level".

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

Still ahead: `sys.exit`, and the `scala.math` surface beyond the basics.

**Not provided, and not scheduled.** `Seq` and `Iterable` are **not** traits of
this dialect and no phase's done-when contains them: `case xs: Seq[_]` and
`x.isInstanceOf[Seq[_]]` are rejected at compile time (D65), and `List`,
`Vector`, `Range`, `Map` and `Set` share no ancestor below `AnyRef` — although
they *do* compare and hash as Scala `Seq`s do, so `List(1,2) == Vector(1,2) ==
(1 to 2)`. `collect` is not provided either (D63): a `PartialFunction` needs the
compiler to emit a second entry point per `{ case … }` literal, which belongs
with Phase 4's pattern-matched `catch`; write `xs.filter(p).map(f)`. There is no
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
| `Priority` | `High`, `Medium`, `Low` | the fields `0`, `1`, `2` (D52) |
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

The prelude gains `Try`/`Success`/`Failure` and `RuntimeError(className,
message)` ahead of Phase 3 (D44).

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
| D31 | Methods cannot be overloaded (a second definition of a name in one template is an error); constructors may be overloaded by number of parameters only | types are erased |
| D32 | Tuples have at most 22 elements (Scala 3 has `TupleXXL`) | `Tuple2`..`Tuple22` are case classes |
| D33 | `Option.getOrElse` evaluates its default eagerly: it is a method reached through a dynamic send, where a by-name parameter is not honoured (D53) | D53 |
| D34 | A pattern-matching function literal `{ case ... }` takes exactly one argument (Scala 3 also accepts it where a function of several parameters is expected) | no expected function type |
| D35 | A refutable pattern generator written without Scala 3's `case` keyword — `for (Some(v) <- os)` — is accepted and filters. scalac 3.9 rejects it ("pattern's type `Some[Int]` is more specialized than the right hand side expression's type `Option[Int]`") and asks for `case`. protoScala accepts the pattern with or without `case` and filters either way | no static types to detect the narrowing |
| D36 | For-comprehension desugaring differs where the result does not: `case` on an *irrefutable* generator pattern emits no `withFilter` step (scalac always inserts one), and `for (x <- xs; y = e)` emits two `map`s instead of dotty's single fused `map`. Both produce the values scalac produces; only the number of intermediate traversals differs | simpler desugaring |
| D37 | An intersection type cannot be tested at run time: `case v: (A & B)` and `x.isInstanceOf[A & B]` are rejected at compile time with "this type cannot be tested at run time", where scalac 3.9 accepts them and tests both components. (The unparenthesised `case v: A & B` is a syntax error in scalac and is rejected here too, with a different message.) A parenthesised *tuple* type, `case v: (Int, Int)`, is accepted here and by scalac, and both test only the tuple's erasure | one marker attribute per class |
| D38 | The default `toString` of an object with no user-written `toString` is `Name@<identity hash>`: a singleton `object O` prints `O@…` where the JVM prints `O$@…`, and printing a companion or a tuple companion directly (`println(Tuple2)`) prints `Tuple2@<hash>` | no JVM name mangling |
| D39 | `List` hash codes differ from the JVM's, while staying consistent with `==` (equal lists have equal hash codes). Case classes, case objects, tuples and strings hash bit-identically to the JVM | `List` is a `ProtoList` |
| D40 | Diagnostic wording: a `var` that redefines a concrete inherited `var` without `override` is reported as "cannot override a mutable variable", where scalac says it "needs `override` modifier". protoScala rejects `override` on a `var` outright, so the two messages describe the same rejected program from opposite ends | `override` is never accepted on a `var` |
| D41 | Scala 3's universal apply (a creator application: `C(args)` standing for `new C(args)`) is **not** synthesised for a plain class. `class C(val a: Int); C(1)` fails with `Not found: C`, where scalac 3.9 compiles it and prints `1`. Write `new C(1)`, or give `C` a companion with an `apply`. Case classes and case objects are unaffected: their companion `apply` is synthesised, so `C(args)` works | no static expected type to place the creator application |
| D42 | A `MatchError` names the Scala class of the unmatched value: `MatchError: 5 (of class Int)`, where the JVM names the boxed class (`scala.MatchError: 5 (of class java.lang.Integer)`). Consistent with D14 (unqualified class names) and D29 (one integer type) | types are erased; there is no boxed class to name |
| D43 | `await` suspends only a chain of protoScala frames each stopped at a call instruction. Inside a native higher-order method (`map`, `foreach`, `withFilter`, a `Future` continuation), or under an instruction that calls back into Scala without being a call site, it raises `UnsupportedOperationException` | a recursive VM cannot snapshot a C++ frame |
| D44 | Until Phase 4 there are no exception values: a failed `Future` carries `RuntimeError(className, message)`, and `Try`/`Success`/`Failure` wrap it | exceptions arrive in Phase 4 |
| D45 | An actor handler returns **either `(newState, reply)` or a bare `newState`**; the bare form means there is no reply to give and the ask's future completes with `()`. A `Tuple2` result is always read as the pair form, so an actor whose state is itself a pair returns it inside one. Only a handler that produces no value at all is rejected, with `IllegalArgumentException` | least surprise: a handler that only updates state should not have to invent a reply |
| D46 | An actor lives as long as the session (it is anchored so the collector can reach it while only the C++ ready stacks refer to it); `Future.apply` creates one actor per call | a removable registry needs `ProtoMap` |
| D47 | `Future.apply` takes its body **by name**: `Future(expr)` and `Future { … }`, as in Scala. By-name parameters are honoured where the compiler can name the callee (D53) | least surprise: Scala's `Future` takes a by-name body |
| D48 | `map`/`flatMap`/`recover`/`onComplete` run their continuation on the thread that completes the future, or on the caller when it is already complete; there is no `ExecutionContext`, and a continuation may not `await` | one scheduling entity |
| D49 | `Thread` and `System` are runtime facilities, not the JVM's; `nanoTime` is monotonic, only differences are meaningful | no JVM |
| D50 | Awaiting a future that fails, inside an actor, abandons the rest of the handler and that message's future inherits the failure | no `try`/`catch` until Phase 4 |
| D51 | `actor.value` reads the state without sending a message, so it may observe a state older than a queued send | a lock-free read |
| D52 | `Priority.High` / `Medium` / `Low` are the integers `0` / `1` / `2` on an object, not an `enum` | `enum` arrives in Phase 4 |
| D53 | A by-name parameter is honoured only where the compiler resolves the call site to the declaration: a call by name to a top-level or local `def` (any parameter list, including a curried one), a method of the template being compiled, a method of an `object`, a class's primary constructor, and a builtin whose by-name signature the runtime declares (`Future.apply`). At any other call site — a method reached through a dynamic send, or a `def` taken as a function value — the argument is evaluated once at the call and each read of the parameter yields that value. scalac resolves all of these statically, so it stays lazy where protoScala does not | dynamic dispatch: a send carries no signature |

| D54 | `s"…"` and `raw"…"` compile directly to a concat opcode, so a user-defined `StringContext` is never consulted. The same input produces the same string | one rope join per interpolation instead of three allocations and two sends |
| D55 | The `f` interpolator supports `%s %b %c %d %o %x %X %e %E %f %g %G %%`, the flags `-`, `+`, space, `0`, `,` and `#`, and `width.precision`. `%n` is not supported — write `\n` — and there is no locale, so the decimal separator is always `.` | a locale means a locale database |
| D56 | An interpolator that is not `s`, `f` or `raw` is a compile error | a custom one needs `extension (sc: StringContext)`, which is Phase 4 |
| D58 | `Map`/`Set` iteration — `toString`, `foreach`, `keys`, `values`, `toList`, `mkString` — is in ascending-hash order: deterministic for a given key set, unrelated to insertion order or to Scala's. The concrete consequence of D7 | Scala guarantees no order either |
| D59 | `Vector.hashCode` equals `List.hashCode` for the same elements, and a `Range`'s equals both; all three differ from the JVM's (D39) | required for `Map(List(1) -> 1)(Vector(1))` to work |
| D61 | `to`, `until` and `by` require bounds that fit a 54-bit integer | matching means boxing every bound, an allocation on every `Range` method |
| D62 | `sorted` orders numbers, `Char`s, strings and booleans with the runtime's own comparison; any other pair raises `IllegalArgumentException: sorted needs comparable elements; use sortWith`. `sorted`, `sortBy` and `sortWith` are stable, as Scala's are | there is no `Ordering` without implicits (D3) |
| D63 | `collect` is not provided; write `xs.filter(p).map(f)` | a `PartialFunction` needs a second entry point per `{ case … }` literal, which belongs with Phase 4 |
| D65 | `Seq` and `Iterable` are not provided: `case xs: Seq[_]` is rejected at compile time, and the collections share no ancestor below `AnyRef` — although they compare and hash as Scala `Seq`s do | no phase's done-when contains them |
| D66 | `%e`, `%f` and `%g` convert through a `Double`, so an integer above 2^53 prints rounded; `%d` is exact | matching means a bignum decimal formatter |
| D67 | Neither `Either` nor `Try` has `filter`/`withFilter`, so `for (x <- e if p)` over one raises `NoSuchMethodError` | Scala's needs a `Left`, or an exception value, that only the static type supplies |
| D68 | `Range.map`, `flatMap` and `filter` answer a `List` where Scala answers an `IndexedSeq`; the elements and their order are Scala's. `Range.reverse` does answer a `Range` | `IndexedSeq` is a `Seq` trait (D65) |
| D69 | `String.split` answers a `List[String]`, not an `Array[String]` | there is no `Array` type (D12) |
| D70 | `String.split` takes a **literal** separator: `"a.b".split(".")` yields `List(a, b)` where Scala, reading a regex, yields `List()`. `split(",")` is identical | matching means a regex engine |
| D71 | A key that overrides `equals` but not `hashCode` misses at once. Scala's `Map1`..`Map4` compare by `==` alone and hide the classic defect up to four entries; at five and beyond Scala answers the same as protoScala | matching means a second, unhashed small-map representation |

D9–D25 are the provisional Phase 1 departures, D28–D42 the provisional
Phase 2 departures, D43–D53 the provisional Phase 5 departures and D54–D71 the
provisional Phase 3 departures listed in
[STATUS.md](STATUS.md#intentional-deviations). The maintainer reviewed the first
three groups on 2026-09-23: approved as recorded, except D45 and D47, which were
overturned (the rows above carry the replacement behaviour) and which brought
D53 with them. The Phase 3 group is **pending review**.

**D57, D60 and D64 do not exist**, and are not reused. They were reserved for a
`Char`-key divergence, a `Range == List` divergence and a `Try.apply` divergence
that the rulings of 2026-09-23 and the arrival of by-name parameters each
removed. Ids are stable references, so the gaps are left open rather than closed
by renumbering.
