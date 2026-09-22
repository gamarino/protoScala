# protoScala Language Reference

> **Status:** target language definition (2026-09-22). What is implemented
> today is tracked in [STATUS.md](STATUS.md). The reference language is
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
| `if`/`then`/`else`, `while`/`do`, blocks as expressions, `return` | 1 |
| lambdas `x => e`, `(x, y) => e`, placeholder syntax `_ + 1` | 1 (placeholders: 2) |
| infix, prefix (`-x`, `!b`, `~n`) and postfix-free method application | 1 |
| string interpolation `s""`, `f""`, `raw""` | 3 |
| `for` comprehensions (generators, guards, value definitions, `yield`) | 2 |
| `match` with the patterns of DESIGN §5.3 | 2 |
| `try`/`catch`/`finally`, `throw` | 4 |
| `import` (selectors, renames `as`, wildcard `*`, `given` imports parsed only) | 1 (UMD prefixes: 6) |
| top-level definitions (no wrapping `object` needed), `@main` methods | 1 |
| `package` clauses (one namespace per file) | 6 |

## 3. Classes and objects (Phase 2)

- `class`, constructor parameters (`val`/`var`/plain), auxiliary constructors
  `def this(...)`, `extends`, `with`, `override`, `abstract`, `final`,
  `sealed`, `open` (accepted), access modifiers (parsed; `private` enforced
  only as a lookup restriction on the defining class — D5).
- `object`, companion objects, `case class`, `case object`, `trait` with
  concrete and abstract members, trait parameters (Scala 3).
- `enum` with simple cases and parameterised cases; `values`, `ordinal`,
  `valueOf` (Phase 4).
- Extension methods `extension (x: T) def m ...` (Phase 4, dispatched on the
  receiver's runtime prototype — D6).
- `super.m`, `super[T].m` in stackable traits (Phase 4).
- `this`, `isInstanceOf[T]`, `asInstanceOf[T]` (checked at run time where `T`
  denotes a class; unchecked for parameterised types — erasure).

## 4. Standard library (Phases 3–5)

- `Any`, `AnyRef`, `Nothing`, `Unit`, `Int`, `Long`, `Double`, `Boolean`,
  `Char`, `String`, `BigInt` (all dynamic, DESIGN §4.1).
- `List` (`Nil`, `::`), `Vector`, `Map`, `Set`, `Range`, `Seq`/`Iterable`
  as traits, `Option`/`Some`/`None`, `Either`/`Left`/`Right`,
  `Try`/`Success`/`Failure`, `TupleN`.
- Common collection methods: `map`, `flatMap`, `filter`, `withFilter`,
  `foreach`, `foldLeft`, `foldRight`, `reduce`, `zip`, `zipWithIndex`,
  `take`, `drop`, `head`, `tail`, `isEmpty`, `size`, `contains`, `exists`,
  `forall`, `find`, `groupBy`, `sortBy`, `sorted`, `mkString`, `toList`,
  `toVector`, `toMap`, `toSet`, `reverse`, `++`, `+:`, `:+`, `updated`,
  `getOrElse`, `get`, `keys`, `values`.
- `println`, `print`, `sys.exit`, `scala.math` basics.
- `Actor`, `Future`, `Priority` (Phase 5).

## 5. Departures from Scala (stable ids; mirrored in STATUS.md)

| Id | Departure | Reason |
|---|---|---|
| D1 | `Int`/`Long` never overflow: results promote to arbitrary precision | protoCore integer model |
| D2 | `Float` is `Double` | protoCore has one floating type |
| D3 | No implicits / givens / `using` resolution (parsed, rejected with a clear error if used) | resolution needs static types |
| D4 | No exhaustiveness or static type checking; type errors surface at run time | types are erased |
| D5 | Access modifiers are advisory except `private` on the defining class | no static checker |
| D6 | Extension methods dispatch on the runtime prototype, not the static type | types are erased |
| D7 | `Map`/`Set` iteration order is unspecified and may differ from Scala's | persistent structures; no ordering guarantee beyond Scala's own (`ListMap`/`SortedMap` are separate types, later) |
| D8 | Java interop (`java.*` classes) is absent; polyglot interop goes through UMD | no JVM |
| D26 | Value discarding (`Unit` expected type) applies only where `Unit` is written on the definition or an ascription, not when it comes from a function type (`val f: Int => Unit = x => x + 1` returns `x + 1`) | types are erased |
| D27 | `@main` methods take no parameters or one `String*` parameter; typed `@main` parameters are rejected | no `FromString` instances without static types |

D9–D25 are the provisional Phase 1 departures listed in
[STATUS.md](STATUS.md#intentional-deviations), pending maintainer review.
