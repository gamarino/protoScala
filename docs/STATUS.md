# protoScala Status

> Living tracker of the gap between [LANGUAGE.md](LANGUAGE.md) and the
> implementation. Update it with every change.
>
> **Current state (2026-09-22):** Phase 1 complete (0.1.0): lexer with the
> Scala 3 offside rule, parser, compiler and VM for values, `val`/`var`/
> `lazy val`/`def`, `if`/`while`, lambdas and closures, recursion with
> `StackOverflowError`, `println`, readline REPL.
> **Tests:** 307 total (`ctest --test-dir build_release -N`) — 199 unit
> (GoogleTest), 94 conformance fixtures, 14 CLI checks. All green. Last
> verified 2026-09-22.

## Implemented

Per [LANGUAGE.md](LANGUAGE.md) §1–§2, the rows delivered in Phase 1:

- [x] Identifiers: alphanumeric, operator, mixed, backquoted.
- [x] Hard and soft keywords (§1's full lists, including `this`).
- [x] Literals: decimal/hex/binary integers with `_` and `L`; floating point;
      characters with escapes and `\u`; strings, triple-quoted strings;
      `s`/`f`/`raw` interpolators lexed as structured tokens (execution
      arrives in Phase 3).
- [x] Comments: `//` line, nested `/* ... */` block.
- [x] Significant indentation (offside rule) and braces, mixable; `end`
      markers.
- [x] `val`, `var`, `lazy val`, `def` (multiple parameter lists, varargs
      `xs: Int*`); default and named arguments are Phase 4.
- [x] `if`/`then`/`else`, `while`/`do`, blocks as expressions, `return`
      (methods only — D11).
- [x] Lambdas `x => e`, `(x, y) => e`; placeholder syntax `_ + 1` is Phase 2.
- [x] Infix, prefix (`-x`, `!b`, `~n`) and postfix-free method application.
- [x] `import` (parsed, ignored until UMD in Phase 6).
- [x] Top-level definitions (no wrapping `object`), `@main` methods.
- [x] Closures with per-activation captures; recursion, including deep and
      mutual recursion, with `StackOverflowError` instead of a crash.
- [x] `println`, `print`, and the methods of `Int`, `Double`, `Boolean`,
      `Char`, `String`, `List` (varargs) and functions (DESIGN §5.1 universal
      `apply`).
- [x] REPL: readline history, multi-line continuation, `:help`, `:quit`,
      `:load`.
- [x] `--disassemble`; conformance, unit and CLI test suites; tutorial
      chapters 1–5 and 14.

## Not yet implemented

Everything in LANGUAGE.md from Phase 2 onward: classes, objects, traits, case
classes, `match`, `for`, collections, string interpolation execution,
exceptions, `enum`, actors, UMD. See [ROADMAP.md](ROADMAP.md).

## Opcode table (mirrors `src/compiler/Opcodes.h`, DESIGN §3.5)

One 32-bit word per instruction: opcode in the low 8 bits, unsigned 24-bit
operand in the high bits. `EXTEND` supplies bits 24..47 of the next
instruction's operand. The numbering is fixed; later phases only append in
their reserved ranges.

| # | Name | Stack effect | Notes |
|---|---|---|---|
| 0 | `NOP` | `[] -> []` | |
| 1 | `EXTEND` | — | operand: high 24 bits of the next instruction's operand |
| 2 | `PUSH_CONST` | `[] -> [consts[operand]]` | |
| 3 | `PUSH_UNIT` | `[] -> [()]` | |
| 4 | `PUSH_NULL` | `[] -> [null]` | |
| 5 | `PUSH_TRUE` | `[] -> [true]` | |
| 6 | `PUSH_FALSE` | `[] -> [false]` | |
| 7 | `POP` | `[v] -> []` | |
| 8 | `DUP` | `[v] -> [v v]` | |
| 9 | `PUSH_LOCAL` | `[] -> [slot[operand]]` | |
| 10 | `STORE_LOCAL` | `[v] -> []` | `slot[operand] = v` |
| 11 | `MAKE_CELL` | `[] -> []` | `slot[operand] = new Cell(null)` |
| 12 | `PUSH_CELL` | `[] -> [slot[operand].value]` | |
| 13 | `STORE_CELL` | `[v] -> []` | `slot[operand].value = v` |
| 14 | `PUSH_GLOBAL` | `[] -> [globals.name]` | operand: a Symbol constant |
| 15 | `STORE_GLOBAL` | `[v] -> []` | operand: a Symbol constant |
| 16 | `MAKE_FN` | `[c1..cn] -> [fn]` | operand: block index; n = its captureCount |
| 17 | `CALL` | `[f a1..an] -> [r]` | operand: n |
| 18 | `CALL_SPREAD` | `[f a1..an list] -> [r]` | operand: n; list elements follow a1..an |
| 19 | `SEND` | `[recv a1..an] -> [r]` | operand: SendSite constant (name, n) |
| 20 | `RETURN` | `[v] -> returns v` | |
| 21 | `MAKE_LAZY` | `[thunk] -> [lazy]` | |
| 22 | `FORCE` | `[v] -> [forced v]` | evaluates a lazy once; other values pass |
| 23 | `JUMP` | control flow | offset in words, from the next instruction |
| 24 | `JUMP_IF_FALSE` | `[b] -> []` | b must be a Boolean |
| 25 | `JUMP_IF_TRUE` | `[b] -> []` | |
| 26 | `JUMP_BACK` | control flow | backward; also a GC safepoint (D-none; Q21, R1) |
| 27 | `ADD` | `[a b] -> [r]` | SmallInteger fast path, protoCore fallback |
| 28 | `SUB` | `[a b] -> [r]` | |
| 29 | `MUL` | `[a b] -> [r]` | |
| 30 | `LT` | `[a b] -> [Boolean]` | |
| 31 | `LE` | `[a b] -> [Boolean]` | |
| 32 | `GT` | `[a b] -> [Boolean]` | |
| 33 | `GE` | `[a b] -> [Boolean]` | |
| 34 | `EQ` | `[a b] -> [Boolean]` | Scala `==` |
| 35 | `NE` | `[a b] -> [Boolean]` | Scala `!=` |
| 36 | `NEG` | `[a] -> [-a]` | |
| 37 | `NOT` | `[b] -> [!b]` | |
| 38..63 | reserved | | Phase 1 additions |
| 64..95 | reserved | | object model, Phase 2: `SEND_SUPER`, `TEST_TYPE`, `TEST_PROTO`, `UNAPPLY_FIELDS` |
| 96..127 | reserved | | exceptions, Phase 4: `THROW` (+ per-module handler table) |
| 128..159 | reserved | | actors, Phase 5: `SEND_ASYNC`, `ASK`, `AWAIT` |

## Intentional deviations

| Id | Deviation | Track |
|---|---|---|
| D1 | `Int`/`Long` promote to arbitrary precision instead of wrapping | (perm) |
| D2 | `Float` is `Double` | (perm) |
| D3 | No implicits / givens resolution | later |
| D4 | No static type checking or exhaustiveness checks | (perm) |
| D5 | Access modifiers advisory except `private` | (perm) |
| D6 | Extension methods dispatch on runtime prototype | (perm) |
| D7 | `Map`/`Set` iteration order unspecified | (perm) |
| D8 | No Java interop | (perm) |

### Provisional deviations (Phase 1) — pending maintainer decision

Each entry names the plan question it answers
([plans/2026-09-22-phase-1-core-language.md](plans/2026-09-22-phase-1-core-language.md),
"Open questions for the maintainer").

| Id | Deviation | Plan question |
|---|---|---|
| D9 | Script mode: top-level statements run in order; top-level vals are initialised eagerly before `@main` (Scala 3 initialises a file's top-level definitions on first access) | Q2 |
| D10 | A bare reference to `def f()` (empty parameter list) evaluates to the function (Scala 3 requires `f()` unless a function type is expected) | Q3 |
| D11 | `return` inside a lambda (non-local return) is rejected | Q5 |
| D12 | Varargs arrive as a `List` (`toString` prints `List(...)`, Scala prints `ArraySeq(...)`) | Q15 |
| D13 | String length and indices count code points, not UTF-16 units; case mapping (`toUpperCase`/`toLowerCase`, `Char.toUpper`/`toLower`) is ASCII-only | Q11 |
| D14 | Runtime errors use unqualified class names (`ArithmeticException`, no `java.lang.`) and the format `file:line: error: Class: message`; arity mismatches raise `IllegalArgumentException` | Q9 |
| D15 | `>>>` raises `UnsupportedOperationException` (integers have no fixed width, D1) | Q12 |
| D16 | Indentation mixing tabs and spaces is rejected | Q13 |
| D17 | Inside `(...)`, a multi-statement lambda body needs braces (no indentation region inside parentheses) | Q1 |
| D18 | A `:` at the end of a line always opens an indentation region (`val x:` + newline + type is rejected) | Q14 |

The following were identified while implementing Phase 1 and were not covered
by a numbered plan question; they follow the same rule (small implementation
detail, not silently decided — recorded here for maintainer review):

| Id | Deviation | Where |
|---|---|---|
| D19 | A negative (or overflowing) shift amount to `<<`/`>>` raises `IllegalArgumentException` rather than being masked to a word width (consistent with D1: no fixed integer width) | `Primitives.cpp::shiftAmount` |
| D20 | `Int.toChar` outside the Unicode code-point range (`< 0` or `> 0x10FFFF`) raises `IllegalArgumentException` | `Primitives.cpp::int_toChar` |
| D21 | `Double.toInt`/`toLong`/`round` of `+Infinity`/`-Infinity` raise `ArithmeticException` (`NaN` converts to `0`, matching the JVM) | `Primitives.cpp::integralToInt` |
| D22 | `Char` predicates (`isDigit`, `isLetter`, `isUpper`, `isLower`, `isWhitespace`) test the ASCII range only, consistently with the ASCII-only case mapping of D13 | `Primitives.cpp` (`asciiDigit`/`asciiUpper`/`asciiLower`) |
| D23 | `case` clauses at the same indentation as their enclosing `match` are not supported; a `case` must be indented deeper than `match` (Scala 3 special-cases the equal-indentation form) | `Layout.cpp` (`lineBreak`: a region only opens on `w > width`) — affects Phase 2 when `match` itself lands |
| D24 | An operator identifier whose first character is a non-ASCII (Unicode, byte `>= 0x80`) code point gets letter precedence (the lowest, as for `unary_-`-style word operators), rather than a precedence derived from Unicode operator-symbol classification | `Parser.cpp::precedence` (`isLetterStart`) |
| D25 | Within one file (or one REPL input), a repeated top-level definition replaces the earlier one (Scala 3 rejects duplicate top-level definitions); a duplicate name inside a block is a compile error. Across REPL inputs a redefinition shadows, as in the Scala REPL: each definition gets its own global key (`x`, then `x#1`, ...) resolved at compile time, so earlier code keeps the binding it saw, a change of kind (`def` → `val`, `val` → `lazy val`) never breaks it, and an input that fails at compile or run time defines nothing | `GlobalTable.h`, `Compiler.cpp::compileUnit`, `Session.cpp::evaluate` |
| D26 | Value discarding applies only where `Unit` is written on the definition (`def f(): Unit`, `return` in it, `val v: Unit`, `(e: Unit)`, `if` without `else`); an expected type `Unit` that comes from a function type (`val f: Int => Unit = x => x + 1`, a lambda passed to an `A => Unit` parameter) does not discard, so the lambda returns its last value (D4: no type checking) | `Desugar.cpp::discardValue` |
| D27 | Typed `@main` parameters (`@main def m(n: Int, s: String)`, parsed from the command line in Scala 3 through `FromString`) are rejected; an `@main` method takes no parameters or one `String*` parameter | `Compiler.cpp::compileUnit` |

## Known issues / platform dependencies

See DESIGN §11 for the full table. Unchanged this phase: R2, R4, R5, R8.

- **R1** — Allocation-free loops have no GC poll: a tight `while` can stall a
  stop-the-world pause. Phase 1 makes a provisional choice (Q21): `JUMP_BACK`
  calls the existing public `ProtoContext::safepoint()` at every loop
  back-edge, where every live value of the frame is in a slot. This is a
  working default, not a resolution of R1, which DESIGN still assigns to the
  maintainer (an agreed back-edge poll API across embedders).
- **R2** — Every `ProtoTuple` is interned and perennial; protoScala never
  maps transient data to it (varargs and captures are `ProtoList`), so R2 is
  a protoClojure/platform-track concern, not a protoScala one.
- **R4** — `createSymbol` leaks for strings longer than 6 bytes (protoClojure
  known issue; shared protoCore code path).
- **R5** — One runtime per process (process-global UMD module cache).
- **R8** — Tagged-pointer budget: 37 of 64 pointer tags and 11 of 16
  embedded types are free today; P1 and P2 take one tag each (35 left).

## Open bugs

None known. 307/307 tests pass (`ctest --test-dir build_release`).

## History

See [CHANGELOG.md](../CHANGELOG.md).
