# protoScala Phase 1 — Core Language Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Run straight-line and functional Scala 3 programs (`val`/`var`/`lazy val`/`def`, `if`/`while`, lambdas and closures, recursion, integer and string arithmetic, `println`) in both brace and significant-indentation syntax, through a lexer with the Scala 3 offside rule, a recursive-descent parser, a bytecode compiler and a protoCore-native VM, from a script runner and a readline REPL.

**Architecture:** `source → Lexer → Layout (INDENT/OUTDENT/NEWLINE) → Parser → AST → Desugar → Compiler → BytecodeModule → ExecutionEngine`, mirroring protoClojure (`src/reader`, `src/compiler`, `src/runtime`, `src/repl`) so its lessons transfer. The AST and bytecode are pure C++ (no `ProtoObject*` except immortal interned symbols); every runtime value lives in a `ProtoContext` slot or a protoCore structure reachable from one; each call runs `ExecutionEngine::execute` natively in its own `ProtoContext`, guarded by a native stack check on a 32 MiB evaluator thread.

**Tech Stack:** C++20, CMake ≥3.20, protoCore (shared lib), GoogleTest 1.14, libreadline

**Spec:** docs/DESIGN.md (+ docs/LANGUAGE.md)

## Global Constraints

- C++20 (`CMAKE_CXX_STANDARD 20`, extensions off), CMake ≥ 3.20, as in the existing `CMakeLists.txt`.
- Every target compiles with `-Wall -Wextra -Wpedantic` and **no warnings**.
- DESIGN §1.1 principles are binding: P1 values live in protoCore structures; P2 one `ProtoContext` per invocation, chained through `previous`; P3 missing capability extends protoCore (as a new type, never by changing an existing type's model); P4 every deviation from Scala gets a stable `D<n>` id in `docs/STATUS.md`; P5 purity over performance; P6 no stop-the-world work beyond protoCore's root scan.
- No `std::` container holds a `ProtoObject*` across an allocation. The only `ProtoObject*` a C++-side structure may keep are (a) interned symbols from `ProtoString::createSymbol` (strong, never collected) and (b) the runtime prototypes pinned in root-context slots for the whole session — each such place carries a comment saying so (protoClojure `src/runtime/BytecodeModule.h:24-33` is the model).
- One `ProtoContext` per invocation (`ExecutionEngine::execute`, native-primitive call scopes).
- Nothing is written outside the workspace: no `/tmp`, no `mktemp` without `-p "$PWD"`; tests create temporaries in the CTest working directory (the build tree), like `tests/conformance/run.sh` already does; an agent's ad-hoc scratch files go in `../.agent_scratch/<task>/` (protoScala `CLAUDE.md`). The REPL touches `~/.protoscala_history` only when stdin is a terminal, so no test writes to `$HOME`; never run `cmake --install` outside the workspace.
- All code comments, messages and documentation in professional English.
- Commits use the repository's configured git identity (`git config user.name` / `user.email`; currently `Gustavo Marino`); never override it with `-c user.*` or `--author`. End each commit message with the attribution line `Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>`.
- Conformance fixtures: first line is a directive (`// EXPECT: <last stdout line>`, `// EXPECT-ERROR[: <substring>]`, `// XFAIL: ...`, `// XFAIL-ERROR...`), one CTest case per file, files starting with `_` are helpers (DESIGN §10, `tests/conformance/run.sh`).
- Types are parsed fully into `TypeTree` and erased; the compiler ignores them (DESIGN §2, §3.3).
- Scala 3 braces **and** significant indentation are supported from the first parser task; every fixture whose syntax differs between the two styles exists in both variants.
- protoScala never maps transient data to `ProtoTuple` (interned and perennial, DESIGN §4.6, R2): argument packs, captures, varargs and every runtime sequence use `ProtoList`.
- `PROTO_NONE` is both Scala `null` and protoCore's "attribute missing" return: presence is probed with `hasOwnAttribute`/`hasAttribute`, never by comparing a lookup result with `PROTO_NONE` alone (DESIGN §4.1).
- Interned symbols are per `ProtoSpace`: never cache them in function-local `static`s (DESIGN §9).
- User-facing output (`--help`, error messages) never mentions internal phase or milestone names (`tests/cli/help.sh` enforces it for `--help`).
- Build and test only with: `cmake -B build_release -S . && cmake --build build_release && ctest --test-dir build_release` (run from `protoScala/`).

---

## Preflight (before Task 1)

- [ ] **Step 1: Confirm the Phase 0 skeleton is committed**

Run: `cd /home/gamarino/Documentos/proyectos/protoScala && git log --oneline | head -3`
Expected: at least one commit containing the Phase 0 skeleton. If the output is
`fatal: ... does not have any commits yet` (the state on 2026-09-22), **stop and ask the
maintainer** to commit Phase 0 first; do not commit the skeleton on their behalf.

- [ ] **Step 2: Confirm the baseline is green**

Run: `cmake -B build_release -S . && cmake --build build_release && ctest --test-dir build_release`
Expected: `100% tests passed` (1 unit, 3 CLI, 1 conformance XFAIL).

- [ ] **Step 3: Confirm libreadline headers are installed**

Run: `test -f /usr/include/readline/readline.h && echo ok`
Expected: `ok`. If missing, ask the maintainer to install `libreadline-dev` (never install packages yourself).

---

## Design notes that apply to several tasks

These are consequences of the spec plus facts found in the code; they are not new design
decisions. Choices that *are* design decisions are listed in **Open questions for the
maintainer** at the end, each with the provisional behaviour this plan implements so the
plan stays executable; every provisional behaviour is reversible and is recorded as a
provisional `D<n>` in `docs/STATUS.md` by Task 13.

1. **protoCore primitive prototypes.** `ProtoSpace::ProtoSpace` (protoCore
   `core/ProtoSpace.cpp:1185-1216`) aliases `smallIntegerPrototype`,
   `largeIntegerPrototype`, `doublePrototype`, `floatPrototype`, `nonePrototype` and
   `methodPrototype` to the shared, mutable `objectPrototype`, and creates `stringPrototype`,
   `booleanPrototype`, `unicodeCharPrototype` and `listPrototype` **immutable**. Installing
   Scala methods "on protoCore's string prototype" (DESIGN §4.1, §6) therefore follows the
   established embedder pattern of protoST (`src/runtime/Bootstrap.cpp:88-103`) and
   protoPython (`src/library/PythonEnvironment.cpp:20181-20182`): protoScala creates its own
   **mutable** prototypes and rebinds the `ProtoSpace` fields to them at start-up
   (`getPrototype` reads those fields, `core/ProtoObject.cpp:310-360`). No protoCore change.
2. **`ProtoObject::call` returns `PROTO_NONE` silently** when the attribute is not a method
   and no `nonMethodCallback` is installed (`core/ProtoObject.cpp:425-435`). The VM therefore
   never uses `call` for Scala sends; it looks the attribute up itself and raises a Scala
   error when it is missing.
3. **Integer semantics already match Scala.** `Integer::divide` truncates toward zero and
   `Integer::modulo` takes the dividend's sign (`core/Integer.cpp:391-437`), which is Java's
   `/` and `%`. Both throw `std::runtime_error("Division by zero.")`; the primitives check for
   zero first and raise `ArithmeticException: / by zero` (Scala's message).
   `Integer::modulo` throws on a double operand, so `Double %` is implemented with `std::fmod`.
4. **SmallInteger range** is `[-(2^53), 2^53-1]`, tag in bits 0–9, value in bits 10–63
   (protoCore `headers/protoCore.h:474-500`: `isSmallInt`, `asSmallInt`, `smallIntInRange`,
   `makeSmallInt`). Fast paths use these public inline helpers (no private tag copies).
5. **Char values** are embedded: tag 1, embedded type 2 (`headers/proto_internal.h:142,251`,
   `core/ProtoContext.cpp:788-794`), so the low 10 bits of a char are `0x081` and the code
   point is `bits >> 10`. protoCore has no public `isUnicodeChar`; `runtime/Values.h` defines
   `isCharFast`/`charValueFast` with a comment pointing at those lines.
6. **Heap limit for GC-pressure tests:** `PROTOCORE_HEAP_LIMIT_CELLS=<hard>` or
   `<soft>,<hard>` (`core/ProtoSpace.cpp:1267-1285`), as protoClojure's
   `tests/cli/bulk-builders-under-heap-limit.sh:51` uses it.
7. **Lessons carried over:** compiler scope stacks are `std::deque` (protoST D29: a
   `std::vector` reallocation left a scope reference dangling); disassemble bytecode before
   blaming caches (a `--disassemble` flag is part of Task 10); REPL modules are retained for
   the whole session because function objects point into them (protoClojure
   `src/repl/Repl.cpp:~283-291`); readline's `RETURN` macro must be `#undef`ined
   (protoClojure `src/repl/Repl.cpp:15-22`).

---

## File Structure

| File | Responsibility |
|---|---|
| `CMakeLists.txt` (modify) | Add static libraries `protoscala_frontend`, `protoscala_compiler`, `protoscala_runtime`, `protoscala_repl` (DESIGN §3.1), the readline check, link the binary; bump version to 0.1.0 (Task 13) |
| `src/frontend/Token.h` | `TokenKind`, `Token`, `SourcePos`, `InterpolationPart`, `tokenKindName` |
| `src/frontend/Lexer.h` / `Lexer.cpp` | Raw tokens: identifiers (alphanumeric, operator, mixed, backquoted), hard keywords, literals (int/float/char/string/triple-quoted/interpolated), nested comments, positions, per-line indentation |
| `src/frontend/UnicodeLetters.h` | Copy of protoClojure's generated table (namespace `protoScala`) for non-ASCII identifier letters |
| `src/frontend/Layout.h` / `Layout.cpp` | Offside rule: inserts `Newline`/`Indent`/`Outdent`/`EndMarker`, converts line-final `:` to `ColonEol`, checks bracket balance; `tokenize()` = Lexer + Layout; `LexError` |
| `src/frontend/AST.h` / `AST.cpp` | AST node structs (no `ProtoObject*`), `TypeTree`, `CompilationUnit`, S-expression `dump()` for tests |
| `src/frontend/Parser.h` / `Parser.cpp` | Recursive descent + precedence climbing over the laid-out token vector; `ParseError` (with `atEof` for REPL continuation); `parseSource()` |
| `src/frontend/Desugar.h` / `Desugar.cpp` | Phase 1 subset of DESIGN §3.4: infix → method call (right-assoc with temp), prefix → `unary_op`, `x op= y`, `if` without `else`, multiple parameter lists → nested lambdas, erasure of `Typed`/`Parens` |
| `src/compiler/Opcodes.h` | The Phase 1 instruction set with fixed numbering, `Instr` word layout, `opName`, `stackEffect` |
| `src/compiler/BytecodeModule.h` / `.cpp` | One compiled function body: code words + line table, de-duplicated constant pool, blocks, capture specs, arity/locals/maxStack, `EXTEND` encoding, symbol linking, disassembler |
| `src/compiler/GlobalTable.h` | Compile-time table of module/session globals and their kinds (val, var, lazy val, def, parameterless def, builtin) |
| `src/compiler/Compiler.h` / `Compiler.cpp` | AST → `BytecodeModule`: scopes, captures (per activation, boxed `var`s and local defs), globals, control flow, calls/sends, fast-path opcodes, stack-depth accounting; `CompileError` |
| `src/runtime/Errors.h` | `ScalaError` (class name + message + innermost line) |
| `src/runtime/StackGuard.h` / `.cpp` | Port of protoClojure's native stack guard; `StackOverflowError` derives from `ScalaError` |
| `src/runtime/Runtime.h` / `.cpp` | `RuntimeLayout` (prototypes, the `unit` singleton, interned keys, globals object) built once per session and pinned in root-context slots; rebinding of `ProtoSpace` primitive prototypes |
| `src/runtime/Values.h` / `.cpp` | Tag helpers, `show` (Scala `toString`), `formatDouble` (Java `Double.toString`), `valuesEqual` (Scala `==`), `typeName` |
| `src/runtime/ExecutionEngine.h` / `.cpp` | Recursive VM: frame = `ProtoContext`, dispatch loop, calls/returns, closures, sends, SmallInt fast paths, thread-local `ActiveCallContext`, line annotation of errors |
| `src/runtime/Primitives.h` / `.cpp` | Native methods: `println`/`print`, methods of `Int`/`Double`/`Boolean`/`Char`/`String`/`List`/`Function`/`Unit`/`Any` |
| `src/repl/Session.h` / `.cpp` | One evaluation session (ProtoSpace + Runtime + globals + retained modules): `runScript`, `evalReplInput`, `loadFile`; shared by script runner and REPL |
| `src/repl/Repl.h` / `.cpp` | readline REPL: prompts, history, multi-line continuation, `:help`/`:quit`/`:load` |
| `src/main.cpp` (modify) | CLI: script runner and REPL on the evaluator thread, `--disassemble`, updated `--help` |
| `tests/unit/CMakeLists.txt` (modify) | Unit test sources and links |
| `tests/unit/test_lexer.cpp`, `test_layout.cpp`, `test_parser.cpp`, `test_desugar.cpp`, `test_bytecode.cpp`, `test_compiler.cpp`, `test_engine.cpp`, `test_values.cpp`, `test_stackguard.cpp` | GoogleTest suites per component |
| `tests/unit/TestSupport.h` | Shared helpers: `kinds()` token-stream string, `evalToString()` |
| `tests/CMakeLists.txt` (modify) | New CLI tests |
| `tests/cli/*.sh` (new) | REPL, script errors, GC pressure, `--disassemble`, history hygiene |
| `tests/conformance/00-binary` … `06-recursion`, `tests/conformance/tutorial/` | Black-box fixtures |
| `examples/hello.scala`, `examples/fib.scala` | Runnable examples |
| `benchmarks/cold-start.sh`, `benchmarks/RESULTS.md` | Self-reporting cold-start measurement |
| `docs/TUTORIAL.md`, `docs/tutorial/*.md` | Dual-audience tutorial chapters 1–5 and 14 |
| `docs/STATUS.md`, `docs/ROADMAP.md`, `docs/LANGUAGE.md`, `CHANGELOG.md`, `README.md`, `LICENSE` | Status, deviations, opcode table, release notes |

Library dependency order (each links `protoscala_support` and protoCore):
`protoscala_frontend` ← `protoscala_compiler` ← `protoscala_runtime` ← `protoscala_repl` ← `protoscala`.
(`BytecodeModule` lives in `src/compiler/` per DESIGN §3.1, so the runtime depends on the
compiler library; the compiler never includes runtime headers.)

---

### Task 1: Tokens and the raw lexer

**Files:**
- Create: `src/frontend/Token.h`, `src/frontend/Lexer.h`, `src/frontend/Lexer.cpp`, `src/frontend/UnicodeLetters.h`
- Create: `tests/unit/TestSupport.h`, `tests/unit/test_lexer.cpp`
- Modify: `CMakeLists.txt` (add `protoscala_frontend`), `tests/unit/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing (first task).
- Produces:
  - `enum class protoScala::TokenKind : uint8_t` (full list below), `struct SourcePos { int line; int column; }`, `struct InterpolationPart`, `struct Token`, `const char* tokenKindName(TokenKind)`.
  - `class protoScala::Lexer { explicit Lexer(std::string source); Token next(); std::vector<Token> tokenizeAll(); }` — raw tokens, no layout tokens; a lexical error is returned as a `TokenKind::Error` token whose `text` is the message and whose `errorAtEof` is true when the input ended inside a construct (unterminated block comment or triple-quoted string).

- [ ] **Step 1: Add the frontend library to the build (empty lexer) and the test helper**

`CMakeLists.txt` — insert after the `protoscala_support` block:

```cmake
# --- Frontend: lexer (with offside rule), parser, AST, desugar ------------------
add_library(protoscala_frontend STATIC
    src/frontend/Lexer.cpp
)
target_link_libraries(protoscala_frontend PUBLIC protoscala_support)
target_compile_options(protoscala_frontend PRIVATE -Wall -Wextra -Wpedantic)
```

`tests/unit/CMakeLists.txt` — replace the executable definition:

```cmake
add_executable(protoscala_unit_tests
    test_smoke.cpp
    test_lexer.cpp
)

target_link_libraries(protoscala_unit_tests
    PRIVATE protoscala_frontend protoscala_support gtest gtest_main)
```

`tests/unit/TestSupport.h`:

```cpp
// Helpers shared by the unit-test suites.
#pragma once
#include "frontend/Lexer.h"

#include <string>
#include <vector>

namespace protoScala::test {

// Space-separated token kinds of `toks`, e.g. "KwVal Identifier Equals IntLit EOF".
inline std::string kinds(const std::vector<Token>& toks) {
    std::string out;
    for (const Token& t : toks) {
        if (!out.empty()) out += ' ';
        out += tokenKindName(t.kind);
    }
    return out;
}

inline std::vector<Token> rawTokens(const std::string& src) {
    return Lexer(src).tokenizeAll();
}

} // namespace protoScala::test
```

Copy the Unicode table: `cp ../protoClojure/src/reader/UnicodeLetters.h src/frontend/UnicodeLetters.h`
and change `namespace protoClojure` to `namespace protoScala` and the first comment line to
"the non-ASCII code points the lexer accepts in alphanumeric identifiers". Keep the
"GENERATED ... do not edit by hand" note.

- [ ] **Step 2: Write the failing lexer tests**

`tests/unit/test_lexer.cpp`:

```cpp
#include "TestSupport.h"

#include <gtest/gtest.h>

using protoScala::Token;
using protoScala::TokenKind;
using protoScala::test::kinds;
using protoScala::test::rawTokens;

TEST(Lexer, EmptyInputIsEof) {
    EXPECT_EQ(kinds(rawTokens("")), "EOF");
}

TEST(Lexer, HardKeywordsAndIdentifiers) {
    EXPECT_EQ(kinds(rawTokens("val x = if then else")),
              "KwVal Identifier Equals KwIf KwThen KwElse EOF");
    EXPECT_EQ(kinds(rawTokens("this def while do return lazy var")),
              "KwThis KwDef KwWhile KwDo KwReturn KwLazy KwVar EOF");
}

TEST(Lexer, SoftKeywordsAreIdentifiers) {
    auto t = rawTokens("end as using inline open");
    for (int i = 0; i < 5; ++i) EXPECT_EQ(t[i].kind, TokenKind::Identifier);
}

TEST(Lexer, OperatorAndMixedIdentifiers) {
    auto t = rawTokens("a+b :: <= unary_- x_+ ! ~");
    EXPECT_EQ(t[0].text, "a");
    EXPECT_EQ(t[1].text, "+");  EXPECT_TRUE(t[1].isOperator);
    EXPECT_EQ(t[2].text, "b");
    EXPECT_EQ(t[3].text, "::");
    EXPECT_EQ(t[4].text, "<=");
    EXPECT_EQ(t[5].text, "unary_-"); EXPECT_FALSE(t[5].isOperator);
    EXPECT_EQ(t[6].text, "x_+");
    EXPECT_EQ(t[7].text, "!");
    EXPECT_EQ(t[8].text, "~");
}

TEST(Lexer, ReservedOperators) {
    EXPECT_EQ(kinds(rawTokens("= => <- <: >: # @ : _ ?=> =>>")),
              "Equals Arrow LeftArrow Subtype Supertype Hash At Colon Underscore "
              "CtxArrow TypeLambdaArrow EOF");
    EXPECT_EQ(kinds(rawTokens("== =>> :+")), "Identifier TypeLambdaArrow Identifier EOF");
}

TEST(Lexer, BackquotedIdentifier) {
    auto t = rawTokens("`type` `hello world`");
    EXPECT_EQ(t[0].kind, TokenKind::Identifier);
    EXPECT_EQ(t[0].text, "type");
    EXPECT_TRUE(t[0].backquoted);
    EXPECT_EQ(t[1].text, "hello world");
}

TEST(Lexer, UnicodeIdentifier) {
    auto t = rawTokens("año = 1");
    EXPECT_EQ(t[0].kind, TokenKind::Identifier);
    EXPECT_EQ(t[0].text, "año");
    EXPECT_EQ(t[1].pos.column, 5);  // columns count code points
}

TEST(Lexer, IntegerLiterals) {
    auto t = rawTokens("42 0x1F 0b101 1_000_000 7L");
    EXPECT_EQ(t[0].intValue, 42);
    EXPECT_EQ(t[1].intValue, 31);
    EXPECT_EQ(t[2].intValue, 5);
    EXPECT_EQ(t[3].intValue, 1000000);
    EXPECT_EQ(t[4].intValue, 7);
    for (int i = 0; i < 5; ++i) EXPECT_EQ(t[i].kind, TokenKind::IntLit);
}

TEST(Lexer, IntegerBeyondLongKeepsDigits) {
    auto t = rawTokens("123456789012345678901234567890");
    EXPECT_EQ(t[0].kind, TokenKind::IntLit);
    EXPECT_FALSE(t[0].fitsLong);
    EXPECT_EQ(t[0].digits, "123456789012345678901234567890");
    EXPECT_EQ(t[0].base, 10);
}

TEST(Lexer, LeadingZeroIsAnError) {
    auto t = rawTokens("012");
    EXPECT_EQ(t[0].kind, TokenKind::Error);
}

TEST(Lexer, FloatLiterals) {
    auto t = rawTokens("1.5 1e3 2.5e-3 .5 3f 4d 1.toString");
    EXPECT_DOUBLE_EQ(t[0].floatValue, 1.5);
    EXPECT_DOUBLE_EQ(t[1].floatValue, 1000.0);
    EXPECT_DOUBLE_EQ(t[2].floatValue, 0.0025);
    EXPECT_DOUBLE_EQ(t[3].floatValue, 0.5);
    EXPECT_EQ(t[4].kind, TokenKind::FloatLit);
    EXPECT_EQ(t[5].kind, TokenKind::FloatLit);
    // `1.toString` is a method call on the integer 1.
    EXPECT_EQ(t[6].kind, TokenKind::IntLit);
    EXPECT_EQ(t[7].kind, TokenKind::Dot);
    EXPECT_EQ(t[8].text, "toString");
}

TEST(Lexer, CharLiterals) {
    auto t = rawTokens(R"('a' '\n' '\'' '\u0041' 'ñ')");
    EXPECT_EQ(t[0].charValue, U'a');
    EXPECT_EQ(t[1].charValue, U'\n');
    EXPECT_EQ(t[2].charValue, U'\'');
    EXPECT_EQ(t[3].charValue, U'A');
    EXPECT_EQ(t[4].charValue, U'ñ');
}

TEST(Lexer, SymbolLiteralIsRejected) {
    EXPECT_EQ(rawTokens("'sym")[0].kind, TokenKind::Error);
}

TEST(Lexer, StringEscapes) {
    auto t = rawTokens(R"("a\tb\n\"q\" \\ \u00e9")");
    EXPECT_EQ(t[0].kind, TokenKind::StringLit);
    EXPECT_EQ(t[0].stringValue, "a\tb\n\"q\" \\ \xC3\xA9");
}

TEST(Lexer, UnclosedStringAtEndOfLineIsAnError) {
    auto t = rawTokens("\"abc\nx");
    EXPECT_EQ(t[0].kind, TokenKind::Error);
    EXPECT_FALSE(t[0].errorAtEof);
}

TEST(Lexer, TripleQuotedStringIsRawAndMultiLine) {
    auto t = rawTokens("\"\"\"a\\n\n  \"b\"\"\"\"\" x");
    EXPECT_EQ(t[0].kind, TokenKind::StringLit);
    EXPECT_EQ(t[0].stringValue, "a\\n\n  \"b\"\"");  // extra quotes belong to the content
    EXPECT_EQ(t[1].text, "x");
}

TEST(Lexer, UnterminatedTripleQuoteIsIncompleteInput) {
    auto t = rawTokens("\"\"\"abc");
    EXPECT_EQ(t[0].kind, TokenKind::Error);
    EXPECT_TRUE(t[0].errorAtEof);
}

TEST(Lexer, InterpolatedStringIsStructured) {
    auto t = rawTokens(R"(s"a $x b ${y + 1} $$")");
    ASSERT_EQ(t[0].kind, TokenKind::InterpolatedString);
    EXPECT_EQ(t[0].text, "s");
    ASSERT_EQ(t[0].parts.size(), 5u);
    EXPECT_FALSE(t[0].parts[0].isHole); EXPECT_EQ(t[0].parts[0].text, "a ");
    EXPECT_TRUE(t[0].parts[1].isHole);  EXPECT_EQ(t[0].parts[1].text, "x");
    EXPECT_EQ(t[0].parts[2].text, " b ");
    EXPECT_TRUE(t[0].parts[3].isHole);  EXPECT_EQ(t[0].parts[3].text, "y + 1");
    EXPECT_EQ(t[0].parts[4].text, " $");
}

TEST(Lexer, NestedBlockComments) {
    EXPECT_EQ(kinds(rawTokens("a /* x /* y */ z */ b // tail\nc")),
              "Identifier Identifier Identifier EOF");
    auto t = rawTokens("/* open /* inner */");
    EXPECT_EQ(t[0].kind, TokenKind::Error);
    EXPECT_TRUE(t[0].errorAtEof);
}

TEST(Lexer, PositionsIndentationAndFirstOnLine) {
    auto t = rawTokens("val x =\n    1 +\n  /* c */ 2");
    EXPECT_EQ(t[0].pos.line, 1); EXPECT_TRUE(t[0].firstOnLine);  EXPECT_EQ(t[0].lineIndent, 0);
    EXPECT_FALSE(t[1].firstOnLine);
    EXPECT_EQ(t[3].pos.line, 2); EXPECT_TRUE(t[3].firstOnLine);  EXPECT_EQ(t[3].lineIndent, 4);
    EXPECT_FALSE(t[4].firstOnLine);
    // A comment before the first token does not change the line's indentation.
    EXPECT_EQ(t[5].pos.line, 3); EXPECT_TRUE(t[5].firstOnLine);  EXPECT_EQ(t[5].lineIndent, 2);
    EXPECT_EQ(t[5].pos.column, 11);
}

TEST(Lexer, MixedTabsAndSpacesInIndentationIsAnError) {
    auto t = rawTokens("a\n\tb\n  c");
    EXPECT_EQ(t.back().kind, TokenKind::Error);
}

TEST(Lexer, Punctuation) {
    EXPECT_EQ(kinds(rawTokens("( ) [ ] { } , ; .")),
              "LParen RParen LBracket RBracket LBrace RBrace Comma Semicolon Dot EOF");
}
```

- [ ] **Step 3: Run the tests to verify they fail**

Run: `cmake -B build_release -S . && cmake --build build_release`
Expected: FAIL — compilation error `frontend/Lexer.h: No such file or directory`.

- [ ] **Step 4: Write `Token.h`**

```cpp
/*
 * Token — one lexical element of Scala 3 source.
 *
 * The raw Lexer produces every kind except the layout kinds (Newline, Indent,
 * Outdent, EndMarker, ColonEol), which Layout inserts (DESIGN §3.2). Soft
 * keywords (as derives end extension infix inline opaque open transparent
 * using) are Identifier tokens; the parser and Layout test their text.
 */
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace protoScala {

struct SourcePos {
    int line = 1;    // 1-based
    int column = 1;  // 1-based, counted in code points
};

enum class TokenKind : uint8_t {
    // Literals
    IntLit, FloatLit, CharLit, StringLit, InterpolatedString,
    // Names (alphanumeric, operator, mixed, backquoted; soft keywords)
    Identifier,
    // Hard keywords (docs/LANGUAGE.md §1, plus `this`)
    KwAbstract, KwCase, KwCatch, KwClass, KwDef, KwDo, KwElse, KwEnum, KwExport,
    KwExtends, KwFalse, KwFinal, KwFinally, KwFor, KwGiven, KwIf, KwImplicit,
    KwImport, KwLazy, KwMatch, KwNew, KwNull, KwObject, KwOverride, KwPackage,
    KwPrivate, KwProtected, KwReturn, KwSealed, KwSuper, KwThen, KwThis, KwThrow,
    KwTrait, KwTrue, KwTry, KwType, KwVal, KwVar, KwWhile, KwWith, KwYield,
    // Punctuation and reserved operators
    LParen, RParen, LBracket, RBracket, LBrace, RBrace,
    Comma, Semicolon, Dot, Colon, Equals, Arrow, CtxArrow, TypeLambdaArrow,
    LeftArrow, Subtype, Supertype, Hash, At, Underscore,
    // Layout (inserted by Layout, never by the Lexer)
    ColonEol, Newline, Indent, Outdent, EndMarker,
    EndOfFile, Error,
};

// One piece of an interpolated string: literal text (escapes already
// processed, except for the `raw` interpolator) or the source text of a
// `$name` / `${expr}` hole.
struct InterpolationPart {
    bool isHole = false;
    std::string text;
    SourcePos pos;
};

struct Token {
    TokenKind kind = TokenKind::Error;
    // Identifier: the name (without backquotes). Keywords and punctuation:
    // their spelling. Literals: the source spelling. InterpolatedString: the
    // interpolator (`s`, `f`, `raw`, ...). EndMarker: the designator (`if`,
    // `while`, a definition name). Error: the message.
    std::string text;
    SourcePos pos;             // first character
    SourcePos end;             // one past the last character
    int  lineIndent = 0;       // width of the leading blanks of the line `pos` is on
    bool firstOnLine = false;  // only blanks and comments precede it on its line
    bool backquoted = false;
    bool isOperator = false;   // Identifier made only of operator characters
    bool errorAtEof = false;   // Error: the input ended inside a construct

    // IntLit: the value when it fits a long long; otherwise `digits` (in
    // `base`, without `_` or suffix) is the exact literal.
    long long intValue = 0;
    bool fitsLong = true;
    std::string digits;
    int base = 10;
    double floatValue = 0.0;              // FloatLit
    char32_t charValue = 0;               // CharLit
    std::string stringValue;              // StringLit: decoded UTF-8
    std::vector<InterpolationPart> parts; // InterpolatedString
};

const char* tokenKindName(TokenKind k);

} // namespace protoScala
```

`tokenKindName` returns the enumerator name without the `TokenKind::` prefix, except
`EndOfFile` → `"EOF"` (the tests above rely on these spellings). Write it as a `switch` over
every enumerator with no `default:` so `-Wall` flags a missing case.

- [ ] **Step 5: Write `Lexer.h`**

```cpp
/*
 * Lexer — Scala 3 source text to raw tokens (no layout tokens).
 *
 * Hand-written, one-character lookahead, same shape as protoClojure's
 * src/reader/Lexer.cpp (advance() counts columns in code points; UTF-8 is
 * decoded with the same decodeUtf8 helper). The Lexer never throws: a
 * lexical error is an Error token and ends the stream.
 */
#pragma once
#include "frontend/Token.h"

#include <string>
#include <vector>

namespace protoScala {

class Lexer {
public:
    explicit Lexer(std::string source);

    // The next raw token. After EndOfFile or Error, keeps returning EndOfFile.
    Token next();

    // Every token up to and including the first EndOfFile or Error.
    std::vector<Token> tokenizeAll();

private:
    std::string source_;
    std::size_t pos_ = 0;
    int line_ = 1;
    int column_ = 1;
    int lastTokenLine_ = 0;          // line of the previous token (0: none yet)
    bool done_ = false;
    std::vector<int> lineIndent_;    // leading-blank width per line (index line-1)
    std::vector<bool> lineHasTab_;   // leading blanks of that line contain a tab
    std::vector<bool> lineHasSpace_; // ... contain a space
    bool sawTabIndent_ = false;
    bool sawSpaceIndent_ = false;

    bool eof() const { return pos_ >= source_.size(); }
    char cur() const { return eof() ? '\0' : source_[pos_]; }
    char peekChar(std::size_t k = 1) const {
        return pos_ + k < source_.size() ? source_[pos_ + k] : '\0';
    }
    void advance();
    void computeLineIndents();
    // Skips blanks and comments. Returns an Error token for an unterminated
    // block comment, else a token with kind EndOfFile meaning "nothing to report".
    Token skipTrivia();

    Token make(TokenKind k, SourcePos start, std::string text);
    Token error(const std::string& msg, SourcePos at, bool atEof = false);

    Token lexIdentifierOrKeyword();
    Token lexOperator();
    Token lexBackquoted();
    Token lexNumber();
    Token lexChar();
    Token lexString();          // "..." or """..."""
    Token lexInterpolated(std::string interpolator, SourcePos start);
    // Reads one escape after the backslash (current char is the one after
    // `\`); appends the UTF-8 encoding to `out`. Returns an error message or "".
    std::string readEscape(std::string& out);
    std::size_t identCharLength(std::size_t at) const;  // 0 when not an ident char
    static bool isOpChar(char c);
};

} // namespace protoScala
```

- [ ] **Step 6: Write `Lexer.cpp`**

The UTF-8 decoder `decodeUtf8` is copied from protoClojure `src/reader/Lexer.cpp:19-49`, and
`advance()` from `src/reader/Lexer.cpp:91-101` (columns count code points). The
algorithmically important parts:

```cpp
#include "frontend/Lexer.h"
#include "frontend/UnicodeLetters.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

namespace protoScala {

namespace {

// decodeUtf8: copy of protoClojure src/reader/Lexer.cpp:19-49.

void appendUtf8(std::string& out, char32_t cp) {
    if (cp < 0x80) { out += static_cast<char>(cp); return; }
    if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
        return;
    }
    out += static_cast<char>(0x80 | (cp & 0x3F));
}

const std::unordered_map<std::string, TokenKind>& keywords() {
    static const std::unordered_map<std::string, TokenKind> table = {
        {"abstract", TokenKind::KwAbstract}, {"case", TokenKind::KwCase},
        {"catch", TokenKind::KwCatch},       {"class", TokenKind::KwClass},
        {"def", TokenKind::KwDef},           {"do", TokenKind::KwDo},
        {"else", TokenKind::KwElse},         {"enum", TokenKind::KwEnum},
        {"export", TokenKind::KwExport},     {"extends", TokenKind::KwExtends},
        {"false", TokenKind::KwFalse},       {"final", TokenKind::KwFinal},
        {"finally", TokenKind::KwFinally},   {"for", TokenKind::KwFor},
        {"given", TokenKind::KwGiven},       {"if", TokenKind::KwIf},
        {"implicit", TokenKind::KwImplicit}, {"import", TokenKind::KwImport},
        {"lazy", TokenKind::KwLazy},         {"match", TokenKind::KwMatch},
        {"new", TokenKind::KwNew},           {"null", TokenKind::KwNull},
        {"object", TokenKind::KwObject},     {"override", TokenKind::KwOverride},
        {"package", TokenKind::KwPackage},   {"private", TokenKind::KwPrivate},
        {"protected", TokenKind::KwProtected}, {"return", TokenKind::KwReturn},
        {"sealed", TokenKind::KwSealed},     {"super", TokenKind::KwSuper},
        {"then", TokenKind::KwThen},         {"this", TokenKind::KwThis},
        {"throw", TokenKind::KwThrow},       {"trait", TokenKind::KwTrait},
        {"true", TokenKind::KwTrue},         {"try", TokenKind::KwTry},
        {"type", TokenKind::KwType},         {"val", TokenKind::KwVal},
        {"var", TokenKind::KwVar},           {"while", TokenKind::KwWhile},
        {"with", TokenKind::KwWith},         {"yield", TokenKind::KwYield},
    };
    return table;  // immutable data, not a ProtoObject: a static is fine
}

TokenKind reservedOperator(const std::string& op) {
    if (op == "=")   return TokenKind::Equals;
    if (op == "=>")  return TokenKind::Arrow;
    if (op == "?=>") return TokenKind::CtxArrow;
    if (op == "=>>") return TokenKind::TypeLambdaArrow;
    if (op == "<-")  return TokenKind::LeftArrow;
    if (op == "<:")  return TokenKind::Subtype;
    if (op == ">:")  return TokenKind::Supertype;
    if (op == "#")   return TokenKind::Hash;
    if (op == "@")   return TokenKind::At;
    if (op == ":")   return TokenKind::Colon;
    return TokenKind::Identifier;
}

} // namespace

Lexer::Lexer(std::string source) : source_(std::move(source)) { computeLineIndents(); }

bool Lexer::isOpChar(char c) {
    return c != '\0' && std::strchr("!#%&*+-/:<=>?@\\^|~", c) != nullptr;
}

// Leading-blank width of every line, and whether tabs or spaces were used.
void Lexer::computeLineIndents() {
    std::size_t i = 0;
    while (true) {
        int width = 0; bool tab = false, space = false;
        while (i < source_.size() && (source_[i] == ' ' || source_[i] == '\t')) {
            (source_[i] == '\t' ? tab : space) = true;
            ++width; ++i;
        }
        lineIndent_.push_back(width);
        lineHasTab_.push_back(tab);
        lineHasSpace_.push_back(space);
        while (i < source_.size() && source_[i] != '\n') ++i;
        if (i >= source_.size()) break;
        ++i;  // past '\n'
    }
}

Token Lexer::make(TokenKind k, SourcePos start, std::string text) {
    Token t;
    t.kind = k;
    t.text = std::move(text);
    t.pos = start;
    t.end = SourcePos{line_, column_};
    t.lineIndent = lineIndent_[start.line - 1];
    t.firstOnLine = (start.line != lastTokenLine_);
    return t;
}

Token Lexer::next() {
    if (done_) { Token t; t.kind = TokenKind::EndOfFile; t.pos = {line_, column_}; return t; }
    Token trivia = skipTrivia();
    if (trivia.kind == TokenKind::Error) { done_ = true; return trivia; }
    SourcePos start{line_, column_};
    if (eof()) {
        done_ = true;
        Token t = make(TokenKind::EndOfFile, start, "");
        t.firstOnLine = true;
        return t;
    }
    // Indentation consistency is checked only on lines that hold tokens.
    if (start.line != lastTokenLine_) {
        if (lineHasTab_[start.line - 1]) sawTabIndent_ = true;
        if (lineHasSpace_[start.line - 1]) sawSpaceIndent_ = true;
        if (sawTabIndent_ && sawSpaceIndent_) {
            done_ = true;
            return error("indentation mixes tabs and spaces", start);
        }
    }
    Token t;
    const char c = cur();
    switch (c) {
        case '(': advance(); t = make(TokenKind::LParen, start, "("); break;
        case ')': advance(); t = make(TokenKind::RParen, start, ")"); break;
        case '[': advance(); t = make(TokenKind::LBracket, start, "["); break;
        case ']': advance(); t = make(TokenKind::RBracket, start, "]"); break;
        case '{': advance(); t = make(TokenKind::LBrace, start, "{"); break;
        case '}': advance(); t = make(TokenKind::RBrace, start, "}"); break;
        case ',': advance(); t = make(TokenKind::Comma, start, ","); break;
        case ';': advance(); t = make(TokenKind::Semicolon, start, ";"); break;
        case '`': t = lexBackquoted(); break;
        case '"': t = lexString(); break;
        case '\'': t = lexChar(); break;
        case '.':
            if (std::isdigit(static_cast<unsigned char>(peekChar()))) { t = lexNumber(); break; }
            advance(); t = make(TokenKind::Dot, start, "."); break;
        default:
            if (std::isdigit(static_cast<unsigned char>(c))) t = lexNumber();
            else if (isOpChar(c)) t = lexOperator();
            else if (identCharLength(pos_) > 0) t = lexIdentifierOrKeyword();
            else t = error(std::string("illegal character '") + c + "'", start);
    }
    if (t.kind == TokenKind::Error) done_ = true;
    lastTokenLine_ = start.line;
    return t;
}

std::vector<Token> Lexer::tokenizeAll() {
    std::vector<Token> out;
    while (true) {
        out.push_back(next());
        const TokenKind k = out.back().kind;
        if (k == TokenKind::EndOfFile || k == TokenKind::Error) return out;
    }
}

// Blanks, newlines, `//` comments and nested `/* */` comments.
Token Lexer::skipTrivia() {
    while (!eof()) {
        const char c = cur();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { advance(); continue; }
        if (c == '/' && peekChar() == '/') {
            while (!eof() && cur() != '\n') advance();
            continue;
        }
        if (c == '/' && peekChar() == '*') {
            const SourcePos start{line_, column_};
            int depth = 0;
            do {
                if (cur() == '/' && peekChar() == '*') { depth++; advance(); advance(); }
                else if (cur() == '*' && peekChar() == '/') { depth--; advance(); advance(); }
                else advance();
            } while (depth > 0 && !eof());
            if (depth > 0) return error("unclosed comment", start, /*atEof=*/true);
            continue;
        }
        break;
    }
    Token none; none.kind = TokenKind::EndOfFile;
    return none;
}

// Alphanumeric identifiers: a letter, `_` or `$`, then letters, digits, `_`,
// `$`; after an `_`, the rest may be operator characters (`unary_-`, `x_+`).
Token Lexer::lexIdentifierOrKeyword() {
    const SourcePos start{line_, column_};
    std::string text;
    bool lastWasUnderscore = false;
    while (!eof()) {
        if (lastWasUnderscore && isOpChar(cur())) {
            while (!eof() && isOpChar(cur())) { text += cur(); advance(); }
            break;
        }
        const std::size_t len = identCharLength(pos_);
        if (len == 0) break;
        lastWasUnderscore = (cur() == '_');
        text.append(source_, pos_, len);
        for (std::size_t i = 0; i < len; ++i) advance();
    }
    // An identifier immediately followed by `"` is a string interpolator.
    if (cur() == '"') return lexInterpolated(text, start);
    if (text == "_") return make(TokenKind::Underscore, start, text);
    const auto& kw = keywords();
    if (auto it = kw.find(text); it != kw.end()) return make(it->second, start, text);
    return make(TokenKind::Identifier, start, text);
}

// Operator identifiers: a maximal run of operator characters, stopping before
// a `//` or `/*` that would start a comment.
Token Lexer::lexOperator() {
    const SourcePos start{line_, column_};
    std::string text;
    while (!eof() && isOpChar(cur())) {
        if (!text.empty() && cur() == '/' && (peekChar() == '/' || peekChar() == '*')) break;
        text += cur();
        advance();
    }
    const TokenKind reserved = reservedOperator(text);
    Token t = make(reserved, start, text);
    t.isOperator = (reserved == TokenKind::Identifier);
    return t;
}
```

The remaining functions follow these exact rules (write them in full; each is a short loop):

- `identCharLength(at)`: ASCII letters, digits, `_`, `$` → 1; a non-ASCII well-formed UTF-8
  sequence whose code point satisfies `isSymbolLetter` (UnicodeLetters.h) → its length; else 0.
- `lexBackquoted()`: consume `` ` ``, read until the next `` ` `` on the same line; empty name
  or newline/EOF first → `error("unclosed quoted identifier")`. Token: `Identifier`,
  `backquoted = true`, `text` = the name.
- `lexNumber()`: `0x`/`0X` → base 16, `0b`/`0B` → base 2, else base 10 (a `0` followed by a
  digit → `error("decimal integer literals may not have a leading zero")`). Digits may contain
  `_` between digits (a `_` at the end or next to `.`/`e` → `error("trailing '_' in number
  literal")`). Base 10 only: `.` followed by a digit starts a fraction; `e`/`E` with optional
  sign and at least one digit starts an exponent; suffix `f`/`F`/`d`/`D` makes it a
  `FloatLit`; suffix `l`/`L` is accepted and ignored. An identifier character right after the
  literal → `error("malformed number literal")`. `IntLit`: strip `_`, `errno = 0;
  strtoll(digits, nullptr, base)`; `ERANGE` → `fitsLong = false` (keep `digits`, `base`) —
  the same approach as protoClojure `src/reader/Lexer.cpp:315-324`. `FloatLit`: `strtod`
  on the stripped text without its suffix. A leading `.` (`.5`) is allowed.
- `readEscape(out)`: `b t n f r " ' \` map to their characters; `u` (one or more `u`s)
  followed by exactly 4 hex digits → that code point via `appendUtf8`; a digit →
  `"octal escape literals are unsupported: use \\u escapes"`; anything else →
  `"invalid escape character"`.
- `lexChar()`: after `'`: `\` → `readEscape`; otherwise one UTF-8 code point; then a closing
  `'` is required. If the character after the first code point is not `'` and the first
  code point is an identifier character → `error("symbol literals are not supported in
  Scala 3")`; `''` → `error("empty character literal")`.
- `lexString()`: `"""` → triple-quoted: content is raw (no escapes), may span lines; the
  string ends at the first `"""` that is **not** followed by another `"` (so `""""a""""`
  is `"a"` plus... the rule is: at a run of k ≥ 3 quotes, the last three close the string
  and the first k−3 belong to the content); EOF first → `error("unclosed multi-line string
  literal", start, /*atEof=*/true)`. Otherwise single-line: escapes via `readEscape`; a
  newline or EOF before the closing `"` → `error("unclosed string literal", start, false)`.
- `lexInterpolated(id, start)`: same delimiters as `lexString`; `$$` → literal `$`;
  `$name` → hole with the identifier's text; `${` → hole with the source text up to the
  matching `}` (counting nested `{`/`}` and skipping nested string literals); any other `$`
  → `error("invalid string interpolation: $$, $ident or ${expr} expected")`. Escapes are
  processed in literal parts unless `id == "raw"` (then backslashes are kept). Parts with
  empty text are omitted. Token: `InterpolatedString`, `text` = `id`.
- `error(msg, at, atEof)`: `Token` with `kind = Error`, `text = msg`, `pos = at`,
  `errorAtEof = atEof`.

- [ ] **Step 7: Run the tests to verify they pass**

Run: `cmake --build build_release && ctest --test-dir build_release -R Lexer --output-on-failure`
Expected: all `Lexer.*` tests PASS; `ctest --test-dir build_release` still 100 %.

- [ ] **Step 8: Commit**

```bash
git add CMakeLists.txt src/frontend tests/unit
git commit -m "frontend: tokens and raw lexer for Scala 3 lexical syntax

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: Offside rule (Layout)

**Files:**
- Create: `src/frontend/Layout.h`, `src/frontend/Layout.cpp`, `tests/unit/test_layout.cpp`
- Modify: `CMakeLists.txt` (add `src/frontend/Layout.cpp` to `protoscala_frontend`), `tests/unit/CMakeLists.txt` (add `test_layout.cpp`)

**Interfaces:**
- Consumes: `Token`, `TokenKind`, `Lexer` (Task 1).
- Produces:
  - `struct LexError : std::runtime_error { SourcePos pos; bool atEof; }`
  - `std::vector<Token> applyLayout(const std::vector<Token>& raw);`
  - `std::vector<Token> tokenize(const std::string& source);` — Lexer + Layout; throws `LexError` for the first lexical or offside error.

The rules implemented (Scala 3 reference, "Optional Braces"), restricted by DESIGN §3.2:

- **Regions.** A stack of regions: `TopLevel` (width = indentation of the first token),
  `Braces` (width = indentation of the `{` line when a token follows `{` on that line,
  otherwise the indentation of the first line inside it), `Parens`, `Brackets`
  (no layout tokens at all inside them — DESIGN §3.2), `Indented` (width pushed by `Indent`).
- **Indent** at a line break when the last token opens a region (`=`, `=>`, `?=>`, `<-`,
  `catch`, `do`, `else`, `finally`, `for`, `if`, `match`, `return`, `then`, `throw`, `try`,
  `while`, `yield`, `with`, a line-final `:` (`ColonEol`), or the closing `)`/`}` of an
  old-style `if (...)`, `while (...)`, `for (...)`/`for {...}` condition) and the next line
  is indented more than the current region.
- **Outdent** at a line break for every `Indented` region whose width is greater than the
  next line's (unless the previous line ended with `then else do catch finally yield match`);
  the next line must then match an enclosing width (else "unindent does not match any outer
  indentation level"). Before `)`, `]`, `}` all `Indented` regions inside the bracket are
  closed. On the same line, `then`/`else`/`do`/`yield`/`catch`/`finally` close an
  `Indented` region opened by their partner keyword (legacy rule). At EOF every `Indented`
  region is closed.
- **Newline** at a line break when the next line has the current region's width (or, in a
  `Braces` region, a smaller width), the previous token can end a statement, the next token
  can begin one, and the next line does not start with a leading infix operator.
- **End markers:** `end` first on its line, followed on the same line by an identifier or
  one of `if while for match try new this val given`, followed by a line break or EOF,
  becomes one `EndMarker` token whose text is the designator.

- [ ] **Step 1: Write the failing layout tests (braces and indentation variants)**

`tests/unit/test_layout.cpp`:

```cpp
#include "frontend/Layout.h"
#include "TestSupport.h"

#include <gtest/gtest.h>

using protoScala::LexError;
using protoScala::tokenize;
using protoScala::test::kinds;

namespace {
std::string lay(const std::string& src) { return kinds(tokenize(src)); }
}

TEST(Layout, StatementsOnSeparateLinesGetNewline) {
    EXPECT_EQ(lay("val a = 1\nval b = 2"),
              "KwVal Identifier Equals IntLit Newline KwVal Identifier Equals IntLit EOF");
}

TEST(Layout, NoNewlineWhenLineCannotEndOrNextCannotBegin) {
    EXPECT_EQ(lay("val a = 1 +\n  2"),
              "KwVal Identifier Equals IntLit Identifier IntLit EOF");
    EXPECT_EQ(lay("f(a,\nb)"), "Identifier LParen Identifier Comma Identifier RParen EOF");
    EXPECT_EQ(lay("x\n.foo"), "Identifier Dot Identifier EOF");
}

TEST(Layout, IndentAfterEqualsAndOutdentAtDedent) {
    EXPECT_EQ(lay("def f =\n  1\ndef g = 2"),
              "KwDef Identifier Equals Indent IntLit Outdent Newline "
              "KwDef Identifier Equals IntLit EOF");
}

TEST(Layout, BraceVariantOfTheSameDefinition) {
    EXPECT_EQ(lay("def f = {\n  1\n}\ndef g = 2"),
              "KwDef Identifier Equals LBrace IntLit RBrace Newline "
              "KwDef Identifier Equals IntLit EOF");
}

TEST(Layout, IndentedBlockStatementsAreSeparatedByNewline) {
    EXPECT_EQ(lay("def f =\n  val a = 1\n  a"),
              "KwDef Identifier Equals Indent KwVal Identifier Equals IntLit Newline "
              "Identifier Outdent EOF");
}

TEST(Layout, IfThenElseIndented) {
    EXPECT_EQ(lay("if c then\n  a\nelse\n  b"),
              "KwIf Identifier KwThen Indent Identifier Outdent KwElse Indent "
              "Identifier Outdent EOF");
}

TEST(Layout, IfThenElseBraces) {
    EXPECT_EQ(lay("if (c) {\n  a\n} else {\n  b\n}"),
              "KwIf LParen Identifier RParen LBrace Identifier RBrace KwElse LBrace "
              "Identifier RBrace EOF");
}

TEST(Layout, OldStyleConditionOpensRegion) {
    EXPECT_EQ(lay("if (c)\n  a\nelse\n  b"),
              "KwIf LParen Identifier RParen Indent Identifier Outdent KwElse Indent "
              "Identifier Outdent EOF");
    EXPECT_EQ(lay("while (c)\n  a"),
              "KwWhile LParen Identifier RParen Indent Identifier Outdent EOF");
}

TEST(Layout, WhileDoIndented) {
    EXPECT_EQ(lay("while c do\n  a\n  b"),
              "KwWhile Identifier KwDo Indent Identifier Newline Identifier Outdent EOF");
}

TEST(Layout, ArrowOpensRegionInsideBraces) {
    EXPECT_EQ(lay("xs.foreach { x =>\n  a\n  b\n}"),
              "Identifier Dot Identifier LBrace Identifier Arrow Indent Identifier Newline "
              "Identifier Outdent RBrace EOF");
}

TEST(Layout, NothingIsInsertedInsideParensOrBrackets) {
    EXPECT_EQ(lay("f(x =>\n    x + 1,\n  2)"),
              "Identifier LParen Identifier Arrow Identifier Identifier IntLit Comma "
              "IntLit RParen EOF");
    EXPECT_EQ(lay("val m: Map[\n  Int,\n  Int] = e"),
              "KwVal Identifier Colon Identifier LBracket Identifier Comma Identifier "
              "RBracket Equals Identifier EOF");
}

TEST(Layout, RegionOpenersFromDesignList) {
    // Every opener of DESIGN §3.2 produces Indent when the next line is deeper.
    const char* openers[] = {"=", "=>", "then", "else", "do", "try", "catch", "finally",
                             "match", "with", "yield", "return", "throw", "<-"};
    for (const char* op : openers) {
        const std::string src = std::string("a ") + op + "\n  b";
        const std::string laid = lay(src);
        EXPECT_NE(laid.find("Indent Identifier Outdent"), std::string::npos)
            << "opener " << op << " gave: " << laid;
    }
}

TEST(Layout, ColonAtEndOfLineOpensTemplateBody) {
    EXPECT_EQ(lay("object A:\n  val x = 1"),
              "KwObject Identifier ColonEol Indent KwVal Identifier Equals IntLit Outdent EOF");
    EXPECT_EQ(lay("object A {\n  val x = 1\n}"),
              "KwObject Identifier LBrace KwVal Identifier Equals IntLit RBrace EOF");
    // A colon followed by a type on the same line stays a Colon.
    EXPECT_EQ(lay("val x: Int = 1"), "KwVal Identifier Colon Identifier Equals IntLit EOF");
}

TEST(Layout, MultipleOutdentsInARow) {
    EXPECT_EQ(lay("def f =\n  if c then\n    a\n  else\n    b\nf"),
              "KwDef Identifier Equals Indent KwIf Identifier KwThen Indent Identifier "
              "Outdent KwElse Indent Identifier Outdent Outdent Newline Identifier EOF");
}

TEST(Layout, OutdentBeforeClosingBrace) {
    EXPECT_EQ(lay("{\n  val a =\n    1\n}"),
              "LBrace KwVal Identifier Equals Indent IntLit Outdent RBrace EOF");
}

TEST(Layout, ElseOnSameLineClosesThenRegion) {
    EXPECT_EQ(lay("if c then\n  a else b"),
              "KwIf Identifier KwThen Indent Identifier Outdent KwElse Identifier EOF");
}

TEST(Layout, ElseOnSameLineDoesNotCloseAnEnclosingBody) {
    EXPECT_EQ(lay("def f =\n  if a then b else c"),
              "KwDef Identifier Equals Indent KwIf Identifier KwThen Identifier KwElse "
              "Identifier Outdent EOF");
}

TEST(Layout, LeadingInfixOperatorContinuesTheLine) {
    EXPECT_EQ(lay("val x = a\n+ b"),
              "KwVal Identifier Equals Identifier Identifier Identifier EOF");
    // Not an infix operator: `-b` is a prefix expression on its own line.
    EXPECT_EQ(lay("a\n-b"), "Identifier Newline Identifier Identifier EOF");
}

TEST(Layout, EndMarkers) {
    EXPECT_EQ(lay("if c then\n  a\nend if\nb"),
              "KwIf Identifier KwThen Indent Identifier Outdent Newline EndMarker Newline "
              "Identifier EOF");
    auto toks = tokenize("def f =\n  1\nend f");
    EXPECT_EQ(toks[toks.size() - 2].kind, protoScala::TokenKind::EndMarker);
    EXPECT_EQ(toks[toks.size() - 2].text, "f");
    // `end` used as an ordinary identifier.
    EXPECT_EQ(lay("val end = 1"), "KwVal Identifier Equals IntLit EOF");
}

TEST(Layout, BracesRegionIgnoresDeeperContinuation) {
    EXPECT_EQ(lay("{\n  a\n    .b\n  c\n}"),
              "LBrace Identifier Dot Identifier Newline Identifier RBrace EOF");
}

TEST(Layout, BadUnindentIsAnError) {
    EXPECT_THROW(tokenize("def f =\n    a\n  b"), LexError);
}

TEST(Layout, UnbalancedBracketsAreErrors) {
    EXPECT_THROW(tokenize("f(a]"), LexError);
    try {
        tokenize("f(a,");
        FAIL() << "expected LexError";
    } catch (const LexError& e) {
        EXPECT_TRUE(e.atEof);  // the REPL keeps reading
    }
}

TEST(Layout, LexicalErrorsBecomeLexError) {
    try {
        tokenize("\"\"\"open");
        FAIL();
    } catch (const LexError& e) {
        EXPECT_TRUE(e.atEof);
    }
}

TEST(Layout, IndentedFirstLineSetsTopLevelWidth) {
    EXPECT_EQ(lay("  a\n  b"), "Identifier Newline Identifier EOF");
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build_release`
Expected: FAIL — `frontend/Layout.h: No such file or directory`.

- [ ] **Step 3: Write `Layout.h`**

```cpp
/*
 * Layout — the Scala 3 offside rule (DESIGN §3.2).
 *
 * Runs over the complete raw token vector and returns a new vector with
 * Newline, Indent, Outdent and EndMarker tokens inserted and line-final `:`
 * turned into ColonEol. Working on the whole vector (instead of lazily)
 * lets the parser look ahead freely; no rule needs parser feedback because
 * the parser-driven cases of the Scala reference (old-style conditions, `:`
 * before a template body) are recognised here from the token context.
 */
#pragma once
#include "frontend/Token.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace protoScala {

struct LexError : std::runtime_error {
    LexError(const std::string& msg, SourcePos p, bool eof)
        : std::runtime_error(msg), pos(p), atEof(eof) {}
    SourcePos pos;
    bool atEof;  // input ended inside a construct: the REPL asks for more lines
};

std::vector<Token> applyLayout(const std::vector<Token>& raw);

std::vector<Token> tokenize(const std::string& source);

} // namespace protoScala
```

- [ ] **Step 4: Write `Layout.cpp` (full state machine)**

```cpp
#include "frontend/Layout.h"
#include "frontend/Lexer.h"

namespace protoScala {

namespace {

enum class RegionKind : uint8_t { TopLevel, Braces, Parens, Brackets, Indented };

struct Region {
    RegionKind kind;
    int width = 0;
    bool widthKnown = true;
    TokenKind opener = TokenKind::EndOfFile;  // Indented: the token that opened it
    bool condition = false;                   // Parens/Braces right after if/while/for
};

bool canEndStatement(TokenKind k) {
    switch (k) {
        case TokenKind::IntLit: case TokenKind::FloatLit: case TokenKind::CharLit:
        case TokenKind::StringLit: case TokenKind::InterpolatedString:
        case TokenKind::Identifier: case TokenKind::KwThis: case TokenKind::KwNull:
        case TokenKind::KwTrue: case TokenKind::KwFalse: case TokenKind::KwReturn:
        case TokenKind::KwType: case TokenKind::Underscore: case TokenKind::RParen:
        case TokenKind::RBracket: case TokenKind::RBrace: case TokenKind::EndMarker:
        case TokenKind::Outdent:
            return true;
        default:
            return false;
    }
}

bool canBeginStatement(TokenKind k) {
    switch (k) {
        case TokenKind::KwCatch: case TokenKind::KwElse: case TokenKind::KwExtends:
        case TokenKind::KwFinally: case TokenKind::KwMatch: case TokenKind::KwWith:
        case TokenKind::KwYield: case TokenKind::KwThen: case TokenKind::KwDo:
        case TokenKind::Comma: case TokenKind::Dot: case TokenKind::Semicolon:
        case TokenKind::Colon: case TokenKind::ColonEol: case TokenKind::Equals:
        case TokenKind::Arrow: case TokenKind::CtxArrow: case TokenKind::TypeLambdaArrow:
        case TokenKind::LeftArrow: case TokenKind::Subtype: case TokenKind::Supertype:
        case TokenKind::Hash: case TokenKind::LBracket: case TokenKind::RParen:
        case TokenKind::RBracket: case TokenKind::RBrace: case TokenKind::EndOfFile:
            return false;
        default:
            return true;
    }
}

bool opensRegion(TokenKind k) {
    switch (k) {
        case TokenKind::Equals: case TokenKind::Arrow: case TokenKind::CtxArrow:
        case TokenKind::LeftArrow: case TokenKind::KwCatch: case TokenKind::KwDo:
        case TokenKind::KwElse: case TokenKind::KwFinally: case TokenKind::KwFor:
        case TokenKind::KwIf: case TokenKind::KwMatch: case TokenKind::KwReturn:
        case TokenKind::KwThen: case TokenKind::KwThrow: case TokenKind::KwTry:
        case TokenKind::KwWhile: case TokenKind::KwYield: case TokenKind::KwWith:
        case TokenKind::ColonEol:
            return true;
        default:
            return false;
    }
}

// The previous line ends with a token that says the statement continues.
bool continuesStatement(TokenKind k) {
    switch (k) {
        case TokenKind::KwThen: case TokenKind::KwElse: case TokenKind::KwDo:
        case TokenKind::KwCatch: case TokenKind::KwFinally: case TokenKind::KwYield:
        case TokenKind::KwMatch:
            return true;
        default:
            return false;
    }
}

// Legacy rule: a closing keyword on the same line closes an Indented region
// opened by its partner. RParen/RBrace stand for an old-style condition.
bool closesRegionOpenedBy(TokenKind closer, TokenKind opener) {
    switch (closer) {
        case TokenKind::KwThen:    return opener == TokenKind::KwIf;
        case TokenKind::KwElse:    return opener == TokenKind::KwThen || opener == TokenKind::RParen;
        case TokenKind::KwDo:      return opener == TokenKind::KwWhile || opener == TokenKind::KwFor;
        case TokenKind::KwYield:   return opener == TokenKind::KwFor || opener == TokenKind::RParen ||
                                          opener == TokenKind::RBrace;
        case TokenKind::KwCatch:   return opener == TokenKind::KwTry;
        case TokenKind::KwFinally: return opener == TokenKind::KwTry || opener == TokenKind::KwCatch;
        default:                   return false;
    }
}

bool isCloser(TokenKind k) {
    return k == TokenKind::RParen || k == TokenKind::RBracket || k == TokenKind::RBrace;
}

bool isEndDesignator(TokenKind k) {
    switch (k) {
        case TokenKind::Identifier: case TokenKind::KwIf: case TokenKind::KwWhile:
        case TokenKind::KwFor: case TokenKind::KwMatch: case TokenKind::KwTry:
        case TokenKind::KwNew: case TokenKind::KwThis: case TokenKind::KwVal:
        case TokenKind::KwGiven:
            return true;
        default:
            return false;
    }
}

class LayoutPass {
public:
    explicit LayoutPass(const std::vector<Token>& raw) : raw_(raw) {}

    std::vector<Token> run() {
        regions_.push_back(Region{RegionKind::TopLevel, raw_.front().lineIndent});
        for (std::size_t i = 0; i < raw_.size(); ++i) {
            const Token& t = raw_[i];
            if (t.kind == TokenKind::Error)
                throw LexError(t.text, t.pos, t.errorAtEof);
            if (t.kind == TokenKind::EndOfFile) {
                while (regions_.back().kind == RegionKind::Indented) {
                    regions_.pop_back();
                    synth(TokenKind::Outdent, t);
                }
                if (regions_.size() > 1)
                    throw LexError(std::string("unclosed '") + openerName(regions_.back().kind) +
                                   "'", t.pos, /*atEof=*/true);
                out_.push_back(t);
                break;
            }
            if (t.firstOnLine && !out_.empty()) lineBreak(i);
            if (t.kind == TokenKind::Identifier && t.text == "end" && t.firstOnLine &&
                tryEndMarker(i)) {
                continue;
            }
            if (!t.firstOnLine) closeByKeyword(t);
            bool closesCondition = false;
            switch (t.kind) {
                case TokenKind::LParen:
                    regions_.push_back(Region{RegionKind::Parens, 0, true, TokenKind::EndOfFile,
                                              isConditionKeyword(prevKind_)});
                    break;
                case TokenKind::LBracket:
                    regions_.push_back(Region{RegionKind::Brackets});
                    break;
                case TokenKind::LBrace: {
                    // `{` followed by a token on the same line (`{ x =>`): the
                    // region's width is the width of this line. `{` at the end of
                    // a line: the width of the first line inside (set by lineBreak).
                    const bool sameLine = !raw_[i + 1].firstOnLine;
                    regions_.push_back(Region{RegionKind::Braces, sameLine ? t.lineIndent : 0,
                                              sameLine, TokenKind::EndOfFile,
                                              prevKind_ == TokenKind::KwFor});
                    break;
                }
                case TokenKind::RParen: case TokenKind::RBracket: case TokenKind::RBrace:
                    closesCondition = closeBracket(t);
                    break;
                default:
                    break;
            }
            Token copy = t;
            if (t.kind == TokenKind::Colon && raw_[i + 1].firstOnLine)
                copy.kind = TokenKind::ColonEol;
            out_.push_back(copy);
            prevKind_ = copy.kind;
            prevClosesCondition_ = closesCondition;
        }
        return std::move(out_);
    }

private:
    const std::vector<Token>& raw_;
    std::vector<Token> out_;
    std::vector<Region> regions_;
    TokenKind prevKind_ = TokenKind::EndOfFile;
    bool prevClosesCondition_ = false;

    static bool isConditionKeyword(TokenKind k) {
        return k == TokenKind::KwIf || k == TokenKind::KwWhile || k == TokenKind::KwFor;
    }

    static const char* openerName(RegionKind k) {
        switch (k) {
            case RegionKind::Parens:   return "(";
            case RegionKind::Brackets: return "[";
            case RegionKind::Braces:   return "{";
            default:                   return "?";
        }
    }

    void synth(TokenKind k, const Token& at) {
        Token s;
        s.kind = k;
        s.pos = at.pos;
        s.end = at.pos;
        s.lineIndent = at.lineIndent;
        out_.push_back(s);
    }

    void lineBreak(std::size_t i) {
        const Token& t = raw_[i];
        if (regions_.back().kind == RegionKind::Parens ||
            regions_.back().kind == RegionKind::Brackets)
            return;  // DESIGN §3.2: no layout tokens inside (...) and [...]
        const int w = t.lineIndent;
        if (regions_.back().kind == RegionKind::Braces && !regions_.back().widthKnown) {
            regions_.back().width = w;
            regions_.back().widthKnown = true;
        }
        const bool conditionOpener =
            prevClosesCondition_ && t.kind != TokenKind::KwThen &&
            t.kind != TokenKind::KwDo && t.kind != TokenKind::KwYield;
        if ((opensRegion(prevKind_) || conditionOpener) && w > regions_.back().width) {
            // An old-style condition is recorded as RParen whether it closed
            // with `)` or (for `for {...}`) with `}`: closesRegionOpenedBy
            // treats both alike.
            const TokenKind opener = conditionOpener ? TokenKind::RParen : prevKind_;
            regions_.push_back(Region{RegionKind::Indented, w, true, opener});
            synth(TokenKind::Indent, t);
            return;
        }
        if (w < regions_.back().width) {
            if (continuesStatement(prevKind_)) return;
            bool popped = false;
            while (regions_.back().kind == RegionKind::Indented && w < regions_.back().width) {
                regions_.pop_back();
                synth(TokenKind::Outdent, t);
                popped = true;
            }
            const Region& cur = regions_.back();
            if (popped && cur.kind != RegionKind::Braces && w > cur.width && !isCloser(t.kind))
                throw LexError("unindent does not match any outer indentation level",
                               t.pos, false);
            const bool widthOk = (w == cur.width) ||
                                 (cur.kind == RegionKind::Braces && w < cur.width);
            if (widthOk && (popped || canEndStatement(prevKind_)) &&
                canBeginStatement(t.kind) && !isLeadingInfix(i))
                synth(TokenKind::Newline, t);
            return;
        }
        if (w == regions_.back().width && canEndStatement(prevKind_) &&
            canBeginStatement(t.kind) && !isLeadingInfix(i))
            synth(TokenKind::Newline, t);
        // w > width after a non-opener: a continuation line, nothing inserted.
    }

    // Scala 3 leading infix operator: an operator identifier followed by a
    // blank and an operand on the same line.
    bool isLeadingInfix(std::size_t i) const {
        const Token& t = raw_[i];
        if (t.kind != TokenKind::Identifier || !t.isOperator || t.backquoted) return false;
        const Token& n = raw_[i + 1];
        if (n.kind == TokenKind::EndOfFile || n.firstOnLine) return false;
        if (n.pos.column <= t.end.column) return false;  // no blank after the operator
        return canBeginStatement(n.kind) && !isCloser(n.kind);
    }

    void closeByKeyword(const Token& t) {
        while (regions_.back().kind == RegionKind::Indented &&
               closesRegionOpenedBy(t.kind, regions_.back().opener)) {
            regions_.pop_back();
            synth(TokenKind::Outdent, t);
        }
    }

    // Returns whether the closed bracket was an old-style condition.
    bool closeBracket(const Token& t) {
        while (regions_.back().kind == RegionKind::Indented) {
            regions_.pop_back();
            synth(TokenKind::Outdent, t);
        }
        const RegionKind want = t.kind == TokenKind::RParen   ? RegionKind::Parens
                              : t.kind == TokenKind::RBracket ? RegionKind::Brackets
                                                              : RegionKind::Braces;
        if (regions_.back().kind != want)
            throw LexError("unbalanced '" + t.text + "'", t.pos, false);
        const bool condition = regions_.back().condition;
        regions_.pop_back();
        return condition;
    }

    bool tryEndMarker(std::size_t& i) {
        const Token& designator = raw_[i + 1];
        if (designator.firstOnLine || !isEndDesignator(designator.kind)) return false;
        const Token& after = raw_[i + 2 < raw_.size() ? i + 2 : raw_.size() - 1];
        if (!(after.firstOnLine || after.kind == TokenKind::EndOfFile)) return false;
        Token m = raw_[i];
        m.kind = TokenKind::EndMarker;
        m.text = designator.text;
        m.end = designator.end;
        out_.push_back(m);
        prevKind_ = TokenKind::EndMarker;
        prevClosesCondition_ = false;
        i += 1;  // the loop's ++i skips the designator
        return true;
    }
};

} // namespace

std::vector<Token> applyLayout(const std::vector<Token>& raw) {
    return LayoutPass(raw).run();
}

std::vector<Token> tokenize(const std::string& source) {
    return applyLayout(Lexer(source).tokenizeAll());
}

} // namespace protoScala
```

(`raw_` always ends with `EndOfFile` or `Error`, and `run()` stops at either, so `raw_[i + 1]`
is in range whenever `raw_[i]` is neither.)

- [ ] **Step 5: Run to verify pass**

Run: `cmake --build build_release && ctest --test-dir build_release -R "Lexer|Layout" --output-on-failure`
Expected: all PASS. If a test's expected stream disagrees with the implementation, re-read
the rule list above; the tests encode the rules, do not weaken a test to match the code.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/frontend/Layout.h src/frontend/Layout.cpp tests/unit
git commit -m "frontend: Scala 3 offside rule (INDENT/OUTDENT/NEWLINE, end markers)

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```


---

### Task 3: AST and the expression parser

**Files:**
- Create: `src/frontend/AST.h`, `src/frontend/AST.cpp`, `src/frontend/Parser.h`, `src/frontend/Parser.cpp`, `tests/unit/test_parser.cpp`
- Modify: `CMakeLists.txt` (add `AST.cpp`, `Parser.cpp` to `protoscala_frontend`), `tests/unit/CMakeLists.txt` (add `test_parser.cpp`)

**Interfaces:**
- Consumes: `tokenize`, `LexError`, `Token`, `TokenKind`, `SourcePos` (Tasks 1–2).
- Produces:
  - AST node structs of `src/frontend/AST.h` (below), `using NodePtr = std::unique_ptr<Node>`, `using TypePtr = std::unique_ptr<TypeTree>`, `struct Param`, `struct CompilationUnit`.
  - `std::string dump(const Node&)`, `std::string dump(const TypeTree&)`, `std::string dump(const CompilationUnit&)`.
  - `struct ParseError : std::runtime_error { SourcePos pos; bool atEof; }`.
  - `class Parser { explicit Parser(std::vector<Token>); NodePtr parseSingleExpression(); std::unique_ptr<CompilationUnit> parseCompilationUnit(); }` (`parseCompilationUnit` is completed in Task 4).
  - `NodePtr parseExpressionSource(const std::string& src);` and `std::unique_ptr<CompilationUnit> parseSource(const std::string& src);` — both convert a `LexError` into a `ParseError` with the same position and `atEof`.

Grammar covered by this task (Scala 3 syntax, Phase 1 subset): literals, identifiers,
prefix `- + ! ~` (a `-` directly before a numeric literal folds into a negative literal),
infix operators by precedence climbing (precedence by first character, lowest to highest:
assignment operators `op=` / letters / `|` / `^` / `&` / `= !` / `< >` / `:` / `+ -` /
`* / %` / other operator characters; right-associative when the operator ends in `:`;
mixing left and right associativity at one precedence is an error; one `Newline` after an
infix operator is skipped), selection `a.b`, application `f(args)` and block arguments
`f { ... }` (same line only), type application `f[T]` (erased later), blocks in braces and
indented blocks, `if`/`then`/`else` (both styles), `while`/`do` (both styles), `return`,
lambdas `x => e`, `_ => e`, `(x, y) => e`, `(x: T) => e`, `() => e`, block lambdas
`{ x => stats }`, assignment `lhs = e`, ascription `e: T`, parenthesised expressions,
tuples (parsed; rejected by the compiler), splices `xs*` / `xs: _*`, named arguments
`f(x = e)`. `match`, `try`, `throw`, `for`, `new`, `this`, `super` and placeholder `_`
raise `ParseError("<feature> is not implemented yet")`.

- [ ] **Step 1: Write the failing parser tests**

`tests/unit/test_parser.cpp`:

```cpp
#include "frontend/AST.h"
#include "frontend/Parser.h"

#include <gtest/gtest.h>

using protoScala::ParseError;
using protoScala::dump;
using protoScala::parseExpressionSource;

namespace {
std::string e(const std::string& src) { return dump(*parseExpressionSource(src)); }

bool incomplete(const std::string& src) {
    try {
        parseExpressionSource(src);
    } catch (const ParseError& err) {
        return err.atEof;
    }
    return false;
}
} // namespace

TEST(Parser, Literals) {
    EXPECT_EQ(e("42"), "(int 42)");
    EXPECT_EQ(e("-5"), "(int -5)");
    EXPECT_EQ(e("2.5"), "(float 2.5)");
    EXPECT_EQ(e("-2.5"), "(float -2.5)");
    EXPECT_EQ(e("\"a\\n\""), "(str \"a\\n\")");
    EXPECT_EQ(e("'c'"), "(char 'c')");
    EXPECT_EQ(e("true"), "true");
    EXPECT_EQ(e("null"), "null");
    EXPECT_EQ(e("()"), "()");
    EXPECT_EQ(e("s\"x $y\""), "(interp s)");
}

TEST(Parser, PrecedenceByFirstCharacter) {
    EXPECT_EQ(e("1 + 2 * 3"), "(infix + (int 1) (infix * (int 2) (int 3)))");
    EXPECT_EQ(e("a - b - c"), "(infix - (infix - a b) c)");
    EXPECT_EQ(e("a max b + 1"), "(infix max a (infix + b (int 1)))");
    EXPECT_EQ(e("a == b && c < d || e"),
              "(infix || (infix && (infix == a b) (infix < c d)) e)");
    EXPECT_EQ(e("x += 1"), "(infix += x (int 1))");
}

TEST(Parser, RightAssociativeColonOperators) {
    EXPECT_EQ(e("a :: b :: c"), "(infix :: a (infix :: b c))");
}

TEST(Parser, MixedAssociativityIsAnError) {
    EXPECT_THROW(parseExpressionSource("a +: b +- c"), ParseError);
}

TEST(Parser, PrefixOperators) {
    EXPECT_EQ(e("-x + !y"), "(infix + (prefix - x) (prefix ! y))");
    EXPECT_EQ(e("~n"), "(prefix ~ n)");
}

TEST(Parser, InfixAcrossLines) {
    EXPECT_EQ(e("a +\n  b"), "(infix + a b)");
    EXPECT_EQ(e("a +\nb"), "(infix + a b)");
    EXPECT_EQ(e("a\n  + b"), "(infix + a b)");
}

TEST(Parser, SelectionApplicationAndTypeArguments) {
    EXPECT_EQ(e("f(1, 2)(3)"), "(apply (apply f (int 1) (int 2)) (int 3))");
    EXPECT_EQ(e("a.b.c(d)"), "(apply (. (. a b) c) d)");
    EXPECT_EQ(e("s.length"), "(. s length)");
    EXPECT_EQ(e("f[Int](x)"), "(apply (tapply f Int) x)");
    EXPECT_EQ(e("f()"), "(apply f)");
}

TEST(Parser, BlockArgumentAndBlockLambda) {
    EXPECT_EQ(e("xs.foreach { x => println(x) }"),
              "(apply (. xs foreach) (block (lambda (x) (block (apply println x)))))");
    EXPECT_EQ(e("xs.foreach { x =>\n  a\n  b\n}"),
              "(apply (. xs foreach) (block (lambda (x) (block a b))))");
}

TEST(Parser, IfBothStyles) {
    EXPECT_EQ(e("if (a) b else c"), "(if a b c)");
    EXPECT_EQ(e("if a then b else c"), "(if a b c)");
    EXPECT_EQ(e("if a then b"), "(if a b)");
    EXPECT_EQ(e("if a then\n  b\nelse\n  c"), "(if a (block b) (block c))");
    EXPECT_EQ(e("if (a) {\n  b\n} else {\n  c\n}"), "(if a (block b) (block c))");
    EXPECT_EQ(e("if (a)\n  b\nelse\n  c"), "(if a (block b) (block c))");
    EXPECT_EQ(e("if (a) || b then c else d"), "(if (infix || (parens a) b) c d)");
    EXPECT_EQ(e("if a then b\nelse c"), "(if a b c)");
}

TEST(Parser, WhileBothStyles) {
    EXPECT_EQ(e("while (i < 10) i += 1"), "(while (infix < i (int 10)) (infix += i (int 1)))");
    EXPECT_EQ(e("while i < 10 do\n  i += 1"),
              "(while (infix < i (int 10)) (block (infix += i (int 1))))");
    EXPECT_EQ(e("while (i < 10) {\n  i += 1\n}"),
              "(while (infix < i (int 10)) (block (infix += i (int 1))))");
}

TEST(Parser, DoWhileIsRejected) {
    EXPECT_THROW(parseExpressionSource("do x while (c)"), ParseError);
}

TEST(Parser, Lambdas) {
    EXPECT_EQ(e("x => x * 2"), "(lambda (x) (infix * x (int 2)))");
    EXPECT_EQ(e("(x: Int, y: Int) => x + y"), "(lambda (x:Int y:Int) (infix + x y))");
    EXPECT_EQ(e("() => 42"), "(lambda () (int 42))");
    EXPECT_EQ(e("_ => 1"), "(lambda (_) (int 1))");
    EXPECT_EQ(e("(f: (Int, String) => Boolean) => f"),
              "(lambda (f:(Int, String) => Boolean) f)");
    EXPECT_EQ(e("x =>\n  f(x)\n  x"), "(lambda (x) (block (apply f x) x))");
}

TEST(Parser, ParensTuplesAscriptionSplicesNamedArgs) {
    EXPECT_EQ(e("(a)"), "(parens a)");
    EXPECT_EQ(e("(a, b)"), "(tuple a b)");
    EXPECT_EQ(e("x: Int"), "(typed x Int)");
    EXPECT_EQ(e("f(xs*)"), "(apply f (splice xs))");
    EXPECT_EQ(e("f(xs: _*)"), "(apply f (splice xs))");
    EXPECT_EQ(e("f(x = 1)"), "(apply f (named x (int 1)))");
}

TEST(Parser, AssignmentAndReturn) {
    EXPECT_EQ(e("x = y + 1"), "(= x (infix + y (int 1)))");
    EXPECT_EQ(e("return"), "(return)");
    EXPECT_EQ(e("return x"), "(return x)");
}

TEST(Parser, TypesAreParsedIntoTypeTrees) {
    EXPECT_EQ(e("x: List[Map[String, Int]]"), "(typed x List[Map[String, Int]])");
    EXPECT_EQ(e("x: (Int, String)"), "(typed x (Int, String))");
    EXPECT_EQ(e("x: Int => Int"), "(typed x (Int) => Int)");
    EXPECT_EQ(e("x: A | B"), "(typed x A | B)");
    EXPECT_EQ(e("x: scala.collection.Seq[?]"), "(typed x scala.collection.Seq[?])");
}

TEST(Parser, LaterPhaseConstructsAreReportedClearly) {
    for (const char* src : {"x match { case 1 => 2 }", "throw e", "try a finally b",
                            "for (x <- xs) yield x", "new A", "this", "_ + 1"}) {
        try {
            parseExpressionSource(src);
            FAIL() << "expected ParseError for " << src;
        } catch (const ParseError& err) {
            EXPECT_NE(std::string(err.what()).find("not implemented yet"), std::string::npos)
                << src << ": " << err.what();
        }
    }
}

TEST(Parser, IncompleteInputIsFlaggedForTheRepl) {
    EXPECT_TRUE(incomplete("1 +"));
    EXPECT_TRUE(incomplete("(1 + "));
    EXPECT_TRUE(incomplete("if a then"));
    EXPECT_TRUE(incomplete("{ a + 1"));
    EXPECT_TRUE(incomplete("\"\"\"abc"));
    EXPECT_FALSE(incomplete("1 + )"));
}

TEST(Parser, ErrorsCarryPositions) {
    try {
        parseExpressionSource("f(1,\n  ]");
        FAIL();
    } catch (const ParseError& err) {
        EXPECT_EQ(err.pos.line, 2);
        EXPECT_EQ(err.pos.column, 3);
    }
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build_release`
Expected: FAIL — `frontend/AST.h: No such file or directory`.

- [ ] **Step 3: Write `AST.h` (complete)**

```cpp
/*
 * AST — the parser's output and the desugarer's input and output.
 *
 * A plain C++ tree owned by the frontend (DESIGN §3.3): it never holds a
 * ProtoObject*; the compiler materialises constants. Nodes carry the source
 * position of their first token. Type syntax is kept as TypeTree so later
 * phases can use it in patterns and isInstanceOf; the compiler ignores it.
 */
#pragma once
#include "frontend/Token.h"

#include <memory>
#include <string>
#include <vector>

namespace protoScala {

struct TypeTree {
    enum class Kind : uint8_t { Name, Applied, Function, Tuple, ByName, Repeated, Wildcard, Infix };
    Kind kind;
    std::string name;  // Name: dotted path; Infix: the operator; Applied: the type constructor
    std::vector<std::unique_ptr<TypeTree>> args;  // Function: params..., result (last)
    SourcePos pos;
};
using TypePtr = std::unique_ptr<TypeTree>;

enum class NodeKind : uint8_t {
    IntLit, FloatLit, StringLit, CharLit, BoolLit, NullLit, UnitLit, InterpString,
    Ident, Select, Apply, TypeApply, Infix, Prefix, Assign, If, While, Return,
    Block, Lambda, Typed, Parens, Tuple, Splice, NamedArg,
    ValDef, DefDef, Import,
};

struct Node {
    Node(NodeKind k, SourcePos p) : kind(k), pos(p) {}
    virtual ~Node() = default;
    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;
    NodeKind kind;
    SourcePos pos;
};
using NodePtr = std::unique_ptr<Node>;

struct IntLit : Node {
    IntLit(SourcePos p) : Node(NodeKind::IntLit, p) {}
    long long value = 0;
    bool fitsLong = true;
    std::string digits;  // exact literal when !fitsLong (with a leading '-' when negative)
    int base = 10;
};
struct FloatLit : Node { FloatLit(SourcePos p) : Node(NodeKind::FloatLit, p) {} double value = 0; std::string text; };
struct StringLit : Node { StringLit(SourcePos p) : Node(NodeKind::StringLit, p) {} std::string value; };
struct CharLit : Node { CharLit(SourcePos p) : Node(NodeKind::CharLit, p) {} char32_t value = 0; };
struct BoolLit : Node { BoolLit(SourcePos p, bool v) : Node(NodeKind::BoolLit, p), value(v) {} bool value; };
struct NullLit : Node { NullLit(SourcePos p) : Node(NodeKind::NullLit, p) {} };
struct UnitLit : Node { UnitLit(SourcePos p) : Node(NodeKind::UnitLit, p) {} };
struct InterpString : Node {
    InterpString(SourcePos p) : Node(NodeKind::InterpString, p) {}
    std::string interpolator;
    std::vector<InterpolationPart> parts;
};
struct Ident : Node {
    Ident(SourcePos p, std::string n) : Node(NodeKind::Ident, p), name(std::move(n)) {}
    std::string name;
};
struct Select : Node {
    Select(SourcePos p, NodePtr q, std::string n)
        : Node(NodeKind::Select, p), qualifier(std::move(q)), name(std::move(n)) {}
    NodePtr qualifier;
    std::string name;
};
struct Apply : Node {
    Apply(SourcePos p, NodePtr f) : Node(NodeKind::Apply, p), fn(std::move(f)) {}
    NodePtr fn;
    std::vector<NodePtr> args;
    bool blockArg = false;  // f { ... }
};
struct TypeApply : Node {
    TypeApply(SourcePos p, NodePtr f) : Node(NodeKind::TypeApply, p), fn(std::move(f)) {}
    NodePtr fn;
    std::vector<TypePtr> types;
};
struct Infix : Node {
    Infix(SourcePos p, NodePtr l, std::string o, NodePtr r)
        : Node(NodeKind::Infix, p), lhs(std::move(l)), op(std::move(o)), rhs(std::move(r)) {}
    NodePtr lhs;
    std::string op;
    NodePtr rhs;
};
struct Prefix : Node {
    Prefix(SourcePos p, std::string o, NodePtr e)
        : Node(NodeKind::Prefix, p), op(std::move(o)), operand(std::move(e)) {}
    std::string op;
    NodePtr operand;
};
struct Assign : Node {
    Assign(SourcePos p, NodePtr t, NodePtr v)
        : Node(NodeKind::Assign, p), target(std::move(t)), value(std::move(v)) {}
    NodePtr target, value;
};
struct If : Node {
    If(SourcePos p) : Node(NodeKind::If, p) {}
    NodePtr cond, thenp, elsep;  // elsep may be null until desugaring
};
struct While : Node { While(SourcePos p) : Node(NodeKind::While, p) {} NodePtr cond, body; };
struct Return : Node { Return(SourcePos p) : Node(NodeKind::Return, p) {} NodePtr value; /* may be null */ };
struct Block : Node {
    Block(SourcePos p) : Node(NodeKind::Block, p) {}
    std::vector<NodePtr> stats;  // statements, in order; the block's value is the last
                                 // one when it is an expression, else ()
};
struct Param {
    std::string name;
    TypePtr type;          // may be null (lambda parameters)
    NodePtr defaultValue;  // may be null
    bool byName = false;   // x: => T
    bool repeated = false; // x: T*
    SourcePos pos;
};
struct Lambda : Node {
    Lambda(SourcePos p) : Node(NodeKind::Lambda, p) {}
    std::vector<Param> params;
    NodePtr body;
};
struct Typed : Node {
    Typed(SourcePos p, NodePtr e, TypePtr t)
        : Node(NodeKind::Typed, p), expr(std::move(e)), type(std::move(t)) {}
    NodePtr expr;
    TypePtr type;
};
struct Parens : Node { Parens(SourcePos p, NodePtr e) : Node(NodeKind::Parens, p), expr(std::move(e)) {} NodePtr expr; };
struct Tuple : Node { Tuple(SourcePos p) : Node(NodeKind::Tuple, p) {} std::vector<NodePtr> elems; };
struct Splice : Node { Splice(SourcePos p, NodePtr e) : Node(NodeKind::Splice, p), expr(std::move(e)) {} NodePtr expr; };
struct NamedArg : Node {
    NamedArg(SourcePos p, std::string n, NodePtr v)
        : Node(NodeKind::NamedArg, p), name(std::move(n)), value(std::move(v)) {}
    std::string name;
    NodePtr value;
};
struct ValDef : Node {
    ValDef(SourcePos p) : Node(NodeKind::ValDef, p) {}
    std::string name;
    bool isVar = false;
    bool isLazy = false;
    TypePtr type;  // may be null
    NodePtr rhs;
};
struct DefDef : Node {
    DefDef(SourcePos p) : Node(NodeKind::DefDef, p) {}
    std::string name;
    std::vector<std::string> annotations;  // e.g. "main", "tailrec"
    std::vector<std::string> typeParams;
    std::vector<std::vector<Param>> paramLists;  // empty: parameterless def
    TypePtr resultType;  // may be null
    NodePtr body;
    bool isMain() const {
        for (const auto& a : annotations) if (a == "main") return true;
        return false;
    }
};
struct Import : Node { Import(SourcePos p) : Node(NodeKind::Import, p) {} std::string text; };

struct CompilationUnit {
    std::vector<NodePtr> stats;
};

// S-expression rendering used by the unit tests (format documented in AST.cpp).
std::string dump(const Node& n);
std::string dump(const TypeTree& t);
std::string dump(const CompilationUnit& u);

// Downcast helper: `as<Apply>(node)`; the caller has checked `kind`.
template <typename T> T& as(Node& n) { return static_cast<T&>(n); }
template <typename T> const T& as(const Node& n) { return static_cast<const T&>(n); }

} // namespace protoScala
```

- [ ] **Step 4: Write `AST.cpp` (dump format)**

The format, which every test in this plan uses:

| Node | Rendering |
|---|---|
| IntLit / FloatLit | `(int <value or digits>)` / `(float <source text, with '-' when negated>)` |
| StringLit / CharLit | `(str "<escaped>")` (escape `\n`, `\t`, `\"`, `\\`) / `(char '<c>')` |
| BoolLit / NullLit / UnitLit | `true` `false` / `null` / `()` |
| InterpString | `(interp <interpolator>)` |
| Ident / Select | `name` / `(. <qualifier> name)` |
| Apply / TypeApply | `(apply <fn> <arg>...)` / `(tapply <fn> <type>...)` |
| Infix / Prefix / Assign | `(infix <op> <lhs> <rhs>)` / `(prefix <op> <e>)` / `(= <target> <value>)` |
| If / While / Return | `(if c t)` or `(if c t e)` / `(while c b)` / `(return)` or `(return v)` |
| Block | `(block <stat>...)` |
| Lambda | `(lambda (<param>...) <body>)`, param = `name` or `name:<type>` |
| Typed / Parens / Tuple / Splice / NamedArg | `(typed e T)` / `(parens e)` / `(tuple a b)` / `(splice e)` / `(named n v)` |
| ValDef | `(val x <rhs>)`, `(var x ...)`, `(lazy-val x ...)`; with a type: `(val x : T <rhs>)`; a `var` without initialiser has no rhs |
| DefDef | `(def [@ann ...] name [[A B]] [(<list>...)] [: T] <body>)`, each list `(<param>...)`, param `name:T`, `name:=> T`, `name:T*`, with default ` = <expr>` appended |
| Import | `(import <text>)` |
| CompilationUnit | `(unit <stat>...)` |
| TypeTree | `Name`, `a.b.C`, `C[A, B]`, `(A, B) => R` (always parenthesised params), `(A, B)`, `=> T`, `T*`, `?`, `A | B` |

Implement as a recursive `void render(std::string& out, const Node&)` with a `switch
(n.kind)` and no `default:` (so `-Wall` reports a missing kind).

- [ ] **Step 5: Write `Parser.h`**

```cpp
/*
 * Parser — hand-written recursive descent with precedence climbing for infix
 * operators (DESIGN §3.3), over the complete laid-out token vector.
 */
#pragma once
#include "frontend/AST.h"
#include "frontend/Token.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace protoScala {

struct ParseError : std::runtime_error {
    ParseError(const std::string& msg, SourcePos p, bool eof)
        : std::runtime_error(msg), pos(p), atEof(eof) {}
    SourcePos pos;
    bool atEof;  // the error is "unexpected end of input": the REPL asks for more
};

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);

    // The whole input must be one expression (unit tests, REPL probes).
    NodePtr parseSingleExpression();

    // Top-level statements of a script or REPL input (Task 4).
    std::unique_ptr<CompilationUnit> parseCompilationUnit();

private:
    std::vector<Token> toks_;
    std::size_t i_ = 0;

    const Token& peek(std::size_t k = 0) const;  // clamps at the final EOF token
    const Token& advance();
    bool at(TokenKind k) const { return peek().kind == k; }
    bool atIdent(const char* text) const;
    const Token& expect(TokenKind k, const char* what);
    [[noreturn]] void fail(const std::string& msg, const Token& at) const;
    [[noreturn]] void unsupported(const std::string& feature, const Token& at) const;
    bool skipOneNewline();  // consumes one Newline or Semicolon if present

    // Expressions
    NodePtr parseExpr();
    NodePtr parseExprOrIndented();
    NodePtr parseIf();
    NodePtr parseWhile();
    NodePtr parseReturn();
    bool lambdaAhead() const;
    std::vector<Param> parseLambdaParams();  // up to and including `=>`
    NodePtr parseLambda();
    NodePtr parseInfix(int minPrec);
    NodePtr parseInfixRest(NodePtr lhs, int minPrec);
    NodePtr parsePrefix();
    NodePtr parseSimple();
    NodePtr parseSimpleRest(NodePtr base);
    NodePtr parseParensExpr();  // `(` ... `)`: unit, parens or tuple
    std::vector<NodePtr> parseArgs();  // after `(`, up to and including `)`
    NodePtr parseBlockExpr();          // `{` ... `}`
    NodePtr parseIndentedBlock();      // Indent ... Outdent
    std::unique_ptr<Block> parseBlockBody(TokenKind terminator, SourcePos pos);
    NodePtr parseBlockStat(TokenKind terminator);
    bool sameLineAhead(TokenKind k) const;

    // Types
    TypePtr parseType();
    TypePtr parseInfixType();
    TypePtr parseSimpleType();

    // Definitions (Task 4)
    NodePtr parseDefinition(std::vector<std::string> annotations);
    NodePtr parseValDef(SourcePos pos, bool isVar, bool isLazy);
    NodePtr parseDefDef(SourcePos pos, std::vector<std::string> annotations);
    std::vector<Param> parseParamClause();
    NodePtr parseImport();
    bool atDefinitionStart() const;
    void checkEndMarker(const Node& previous, const Token& marker) const;
};

int precedence(const std::string& op);
bool isRightAssociative(const std::string& op);
bool isAssignmentOperator(const std::string& op);

NodePtr parseExpressionSource(const std::string& src);
std::unique_ptr<CompilationUnit> parseSource(const std::string& src);

} // namespace protoScala
```

- [ ] **Step 6: Write `Parser.cpp` — the algorithmically important parts**

```cpp
#include "frontend/Parser.h"
#include "frontend/Layout.h"

namespace protoScala {

namespace {
bool isLetterStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$' ||
           static_cast<unsigned char>(c) >= 0x80;
}
} // namespace

bool isAssignmentOperator(const std::string& op) {
    if (op.size() < 2 || op.back() != '=' || op.front() == '=') return false;
    if (op == "<=" || op == ">=" || op == "!=") return false;
    for (char c : op) if (isLetterStart(c) || std::isdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

bool isRightAssociative(const std::string& op) { return !op.empty() && op.back() == ':'; }

// Scala precedence by first character, higher binds tighter.
int precedence(const std::string& op) {
    if (isAssignmentOperator(op)) return 0;
    const char c = op.front();
    if (isLetterStart(c)) return 1;
    switch (c) {
        case '|': return 2;
        case '^': return 3;
        case '&': return 4;
        case '=': case '!': return 5;
        case '<': case '>': return 6;
        case ':': return 7;
        case '+': case '-': return 8;
        case '*': case '/': case '%': return 9;
        default: return 10;
    }
}

Parser::Parser(std::vector<Token> tokens) : toks_(std::move(tokens)) {}

const Token& Parser::peek(std::size_t k) const {
    const std::size_t j = i_ + k;
    return j < toks_.size() ? toks_[j] : toks_.back();
}

const Token& Parser::advance() {
    const Token& t = peek();
    if (i_ < toks_.size() - 1) ++i_;
    return t;
}

void Parser::fail(const std::string& msg, const Token& at) const {
    const bool eof = at.kind == TokenKind::EndOfFile;
    throw ParseError(eof ? "unexpected end of input" + (msg.empty() ? "" : ": " + msg) : msg,
                     at.pos, eof);
}

void Parser::unsupported(const std::string& feature, const Token& at) const {
    throw ParseError(feature + " is not implemented yet", at.pos, false);
}

const Token& Parser::expect(TokenKind k, const char* what) {
    if (!at(k))
        fail(std::string(what) + " expected but '" + peek().text + "' found", peek());
    return advance();
}

bool Parser::skipOneNewline() {
    if (at(TokenKind::Newline) || at(TokenKind::Semicolon)) { advance(); return true; }
    return false;
}

NodePtr Parser::parseSingleExpression() {
    NodePtr e = parseExpr();
    while (skipOneNewline()) {}
    if (!at(TokenKind::EndOfFile))
        fail("end of expression expected but '" + peek().text + "' found", peek());
    return e;
}

NodePtr Parser::parseExprOrIndented() {
    if (at(TokenKind::Indent)) return parseIndentedBlock();
    return parseExpr();
}

NodePtr Parser::parseExpr() {
    const Token& t = peek();
    switch (t.kind) {
        case TokenKind::KwIf:     return parseIf();
        case TokenKind::KwWhile:  return parseWhile();
        case TokenKind::KwReturn: return parseReturn();
        case TokenKind::KwDo:
            fail("do-while loops are not part of Scala 3; use while ... do", t);
        case TokenKind::KwThrow:  unsupported("throw", t);
        case TokenKind::KwTry:    unsupported("try", t);
        case TokenKind::KwFor:    unsupported("for comprehension", t);
        default: break;
    }
    if (lambdaAhead()) return parseLambda();
    NodePtr e = parseInfix(0);
    if (at(TokenKind::KwMatch)) unsupported("match", peek());
    if (at(TokenKind::Equals)) {
        const NodeKind k = e->kind;
        if (k != NodeKind::Ident && k != NodeKind::Select && k != NodeKind::Apply)
            fail("left-hand side of an assignment must be a name", peek());
        const SourcePos p = e->pos;
        advance();
        return std::make_unique<Assign>(p, std::move(e), parseExprOrIndented());
    }
    if (at(TokenKind::Colon)) {
        const SourcePos p = e->pos;
        advance();
        return std::make_unique<Typed>(p, std::move(e), parseType());
    }
    return e;
}

// if (c) t [else e] | if c then t [else e]
NodePtr Parser::parseIf() {
    auto node = std::make_unique<If>(advance().pos);
    if (at(TokenKind::LParen)) {
        NodePtr cond = parseParensExpr();
        if (at(TokenKind::KwThen)) {
            advance();
            node->cond = std::move(cond);
        } else if (sameLineAhead(TokenKind::KwThen)) {
            // `if (a) || b then ...`: the parenthesised part starts a longer condition.
            node->cond = parseInfixRest(parseSimpleRest(std::move(cond)), 0);
            expect(TokenKind::KwThen, "'then'");
        } else {
            // Old style: the condition is the parenthesised expression.
            if (cond->kind == NodeKind::Parens) cond = std::move(as<Parens>(*cond).expr);
            node->cond = std::move(cond);
            if (at(TokenKind::Newline) && peek(1).kind != TokenKind::KwElse) advance();
        }
    } else {
        node->cond = parseExprOrIndented();
        expect(TokenKind::KwThen, "'then'");
    }
    node->thenp = parseExprOrIndented();
    const std::size_t save = i_;
    skipOneNewline();
    if (at(TokenKind::KwElse)) {
        advance();
        node->elsep = parseExprOrIndented();
    } else {
        i_ = save;
    }
    return node;
}

// while (c) body | while c do body
NodePtr Parser::parseWhile() {
    auto node = std::make_unique<While>(advance().pos);
    if (at(TokenKind::LParen)) {
        NodePtr cond = parseParensExpr();
        if (at(TokenKind::KwDo)) {
            advance();
            node->cond = std::move(cond);
        } else if (sameLineAhead(TokenKind::KwDo)) {
            node->cond = parseInfixRest(parseSimpleRest(std::move(cond)), 0);
            expect(TokenKind::KwDo, "'do'");
        } else {
            if (cond->kind == NodeKind::Parens) cond = std::move(as<Parens>(*cond).expr);
            node->cond = std::move(cond);
            if (at(TokenKind::Newline)) advance();
        }
    } else {
        node->cond = parseExprOrIndented();
        expect(TokenKind::KwDo, "'do'");
    }
    node->body = parseExprOrIndented();
    return node;
}

NodePtr Parser::parseReturn() {
    auto node = std::make_unique<Return>(advance().pos);
    const TokenKind k = peek().kind;
    const bool hasValue = k != TokenKind::Newline && k != TokenKind::Semicolon &&
                          k != TokenKind::Outdent && k != TokenKind::RBrace &&
                          k != TokenKind::RParen && k != TokenKind::KwElse &&
                          k != TokenKind::EndOfFile;
    if (hasValue) node->value = parseExprOrIndented();
    return node;
}

// `x =>`, `_ =>`, or `( ... ) =>` with the matching parenthesis followed by `=>`.
bool Parser::lambdaAhead() const {
    const TokenKind k0 = peek().kind;
    if ((k0 == TokenKind::Identifier && !peek().isOperator) || k0 == TokenKind::Underscore)
        return peek(1).kind == TokenKind::Arrow;
    if (k0 != TokenKind::LParen) return false;
    int depth = 0;
    for (std::size_t k = 0;; ++k) {
        const TokenKind kk = peek(k).kind;
        if (kk == TokenKind::EndOfFile) return false;
        if (kk == TokenKind::LParen) ++depth;
        if (kk == TokenKind::RParen && --depth == 0) return peek(k + 1).kind == TokenKind::Arrow;
    }
}

std::vector<Param> Parser::parseLambdaParams() {
    std::vector<Param> params;
    auto one = [&]() {
        Param p;
        p.pos = peek().pos;
        if (at(TokenKind::Underscore)) { advance(); p.name = "_"; }
        else p.name = expect(TokenKind::Identifier, "parameter name").text;
        if (at(TokenKind::Colon)) { advance(); p.type = parseType(); }
        params.push_back(std::move(p));
    };
    if (at(TokenKind::LParen)) {
        advance();
        if (!at(TokenKind::RParen)) {
            one();
            while (at(TokenKind::Comma)) { advance(); one(); }
        }
        expect(TokenKind::RParen, "')'");
    } else {
        one();
    }
    expect(TokenKind::Arrow, "'=>'");
    return params;
}

NodePtr Parser::parseLambda() {
    auto node = std::make_unique<Lambda>(peek().pos);
    node->params = parseLambdaParams();
    node->body = parseExprOrIndented();
    return node;
}

NodePtr Parser::parseInfix(int minPrec) { return parseInfixRest(parsePrefix(), minPrec); }

NodePtr Parser::parseInfixRest(NodePtr lhs, int minPrec) {
    int lastPrec = -1;
    bool lastRight = false;
    while (at(TokenKind::Identifier)) {
        const Token& opTok = peek();
        const std::string op = opTok.text;
        // `xs*` at the end of an argument is a splice, not multiplication.
        if (op == "*" && peek(1).kind == TokenKind::RParen) break;
        const int p = precedence(op);
        if (p < minPrec) break;
        const bool right = isRightAssociative(op);
        if (p == lastPrec && right != lastRight)
            fail("left- and right-associative operators with the same precedence may not "
                 "be mixed", opTok);
        advance();
        if (at(TokenKind::Newline)) advance();  // InfixExpr ::= InfixExpr id [nl] InfixExpr
        const TokenKind next = peek().kind;
        if (next == TokenKind::RParen || next == TokenKind::RBrace ||
            next == TokenKind::Outdent || next == TokenKind::Comma)
            fail("postfix operators are not supported; '" + op + "' needs a right operand",
                 peek());
        NodePtr rhs = parseInfix(right ? p : p + 1);
        const SourcePos pos = lhs->pos;
        lhs = std::make_unique<Infix>(pos, std::move(lhs), op, std::move(rhs));
        lastPrec = p;
        lastRight = right;
    }
    return lhs;
}

NodePtr Parser::parsePrefix() {
    const Token& t = peek();
    if (t.kind == TokenKind::Identifier &&
        (t.text == "-" || t.text == "+" || t.text == "!" || t.text == "~")) {
        const Token& operand = peek(1);
        const bool adjacent = operand.pos.line == t.pos.line;
        if (t.text == "-" && adjacent &&
            (operand.kind == TokenKind::IntLit || operand.kind == TokenKind::FloatLit)) {
            advance();
            NodePtr lit = parseSimple();  // the literal plus any `.method` suffix
            // Fold the sign into the literal itself (so Long.MinValue-style
            // literals work); a suffix such as `-1.abs` applies to the literal.
            Node* target = lit.get();
            while (target->kind == NodeKind::Select) target = as<Select>(*target).qualifier.get();
            while (target->kind == NodeKind::Apply) target = as<Apply>(*target).fn.get();
            if (target->kind == NodeKind::IntLit) {
                auto& il = as<IntLit>(*target);
                il.value = -il.value;
                if (!il.fitsLong) il.digits = "-" + il.digits;
                il.pos = t.pos;
            } else if (target->kind == NodeKind::FloatLit) {
                auto& fl = as<FloatLit>(*target);
                fl.value = -fl.value;
                fl.text = "-" + fl.text;
                fl.pos = t.pos;
            }
            lit->pos = t.pos;
            return lit;
        }
        advance();
        return std::make_unique<Prefix>(t.pos, t.text, parseSimple());
    }
    return parseSimple();
}
```

Note on the sign fold: Scala's `-1.abs` is `(-1).abs` = 1 only because `-1` is a literal;
the fold above implements exactly that. (`- x.abs` with an identifier stays
`(prefix - (. x abs))`.)

The remaining parser functions follow these rules — write them in full:

- `parseSimple()`: literal tokens → literal nodes (`IntLit` copies `intValue`, `fitsLong`,
  `digits`, `base`; `FloatLit` copies `floatValue` and the source `text`); `Identifier` →
  `Ident`; `KwTrue`/`KwFalse`/`KwNull`; `InterpolatedString` → `InterpString`; `LParen` →
  `parseParensExpr()`; `LBrace` → `parseBlockExpr()`; `KwNew` → `unsupported("new")`;
  `KwThis`/`KwSuper` → `unsupported("this")`/`unsupported("super")`; `Underscore` →
  `unsupported("placeholder syntax '_'")`; `KwMatch` → `unsupported("match")`; any other
  token → `fail("expression expected but '<text>' found")` (EOF gives `atEof`). Then
  `return parseSimpleRest(base)`.
- `parseSimpleRest(base)`: loop — `Dot` then `Identifier` → `Select`; `LParen` **not**
  `firstOnLine` → `Apply` with `parseArgs()`; `LBrace` not `firstOnLine` → `Apply` with one
  `parseBlockExpr()` argument and `blockArg = true`; `LBracket` → `TypeApply` with
  comma-separated `parseType()` until `]`; otherwise stop.
- `parseParensExpr()`: `(` `)` → `UnitLit`; one expression → `Parens`; several
  comma-separated → `Tuple`.
- `parseArgs()` (after `(`): until `)`: `Identifier` followed by `Equals` → `NamedArg`;
  otherwise `parseExpr()`; then `Identifier "*"` before `)` → wrap in `Splice`; a `Typed`
  whose type is `Repeated(Wildcard)` (`xs: _*`) → `Splice`. `parseType` recognises `_*` in
  this position: after `Underscore`, an `Identifier "*"` makes `Repeated(Wildcard)`.
- `parseBlockExpr()`: `expect(LBrace)`; `parseBlockBody(RBrace, pos)`; `expect(RBrace)`.
- `parseIndentedBlock()`: `expect(Indent)`; `parseBlockBody(Outdent, pos)`; `expect(Outdent)`.
- `parseBlockBody(terminator)`: loop { skip every `Newline`/`Semicolon`; stop at
  `terminator` (or EOF → `fail` with `atEof`); if `EndMarker` → `checkEndMarker(*stats.back(),
  marker)`, consume, continue; `stats.push_back(parseBlockStat(terminator))`; then the next
  token must be `Newline`, `Semicolon`, `EndMarker`-after-`Newline`, or `terminator`, else
  `fail("';' or newline expected but '<text>' found")` }.
- `parseBlockStat(terminator)`: (Task 3 version) if `lambdaAhead()`: parse the parameters
  with `parseLambdaParams()`, then the body is `parseIndentedBlock()` when the next token is
  `Indent`, otherwise `parseBlockBody(terminator)` — the rest of the block (Scala's
  `ResultExpr ::= Bindings '=>' Block`). Otherwise `parseExpr()`.
- `sameLineAhead(k)`: scan from the current token while the token is not `firstOnLine`
  (except the first), not a layout token and not EOF, tracking `()[]{}` depth; true when
  `k` is found at depth 0.
- `parseType()`: `LParen` → comma-separated types until `)`; followed by `Arrow` →
  `Function(params..., parseType())`; else one type → that type, several → `Tuple`.
  Otherwise `t = parseInfixType()`; followed by `Arrow` → `Function(t, parseType())`.
  `parseInfixType()`: `parseSimpleType()` then while `Identifier` with text `|` or `&` →
  `Infix`. `parseSimpleType()`: `Identifier` (then `Dot Identifier`...) → `Name`;
  `Underscore` or `Identifier "?"` → `Wildcard`; then `LBracket` → `Applied` with
  comma-separated types; anything else → `fail("type expected")`.
- `parseExpressionSource(src)`: `try { Parser p(tokenize(src)); return p.parseSingleExpression(); }
  catch (const LexError& e) { throw ParseError(e.what(), e.pos, e.atEof); }`;
  `parseSource` the same with `parseCompilationUnit()`.

- [ ] **Step 7: Run to verify pass**

Run: `cmake --build build_release && ctest --test-dir build_release -R Parser --output-on-failure`
Expected: all `Parser.*` PASS.

- [ ] **Step 8: Commit**

```bash
git add CMakeLists.txt src/frontend tests/unit
git commit -m "frontend: AST and expression parser (precedence climbing, both syntaxes)

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 4: Definitions and compilation units

**Files:**
- Modify: `src/frontend/Parser.cpp`, `tests/unit/test_parser.cpp`

**Interfaces:**
- Consumes: everything of Task 3.
- Produces: `Parser::parseCompilationUnit()` complete; `parseBlockStat` accepts local definitions; `parseSource(src)` usable by the compiler and the session.

Rules: a top-level statement is `{Annotation} {Modifier} (val | var | lazy val | def |
import)` or an expression (script mode — see Open question Q2). Annotations: `@` +
identifier (arguments in parentheses, if any, are skipped). Modifiers `private`,
`protected`, `final`, `override`, `abstract`, `sealed`, `open`, `inline`, `transparent`,
`infix`, `opaque` are parsed and ignored (D5); `implicit`, `given` and a `using` parameter
clause raise `ParseError("implicits and givens are not supported (D3)")`. `class`,
`object`, `trait`, `enum`, `case`, `type`, `extension`, `package`, `export` raise
"`<keyword>` definitions are not implemented yet". A `val`/`var` pattern other than a
single name raises "patterns in val definitions are not implemented yet". Scala 2
procedure syntax `def f() { ... }` raises "procedure syntax is not supported in Scala 3;
write `def f(): Unit = ...`". End markers are verified: `end if` / `end while` after an
`If` / `While`, `end <name>` after a `def` or `val` of that name, `end val` after a val;
anything else raises "misaligned end marker".

- [ ] **Step 1: Add the failing definition tests to `tests/unit/test_parser.cpp`**

```cpp
namespace {
std::string u(const std::string& src) { return dump(*protoScala::parseSource(src)); }

std::string unitError(const std::string& src) {
    try {
        protoScala::parseSource(src);
    } catch (const ParseError& err) {
        return err.what();
    }
    return "";
}
} // namespace

TEST(ParserDefs, ValVarLazy) {
    EXPECT_EQ(u("val x: Int = 1"), "(unit (val x : Int (int 1)))");
    EXPECT_EQ(u("var y = 2"), "(unit (var y (int 2)))");
    EXPECT_EQ(u("lazy val z = f()"), "(unit (lazy-val z (apply f)))");
}

TEST(ParserDefs, DefShapes) {
    EXPECT_EQ(u("def add(a: Int, b: Int): Int = a + b"),
              "(unit (def add ((a:Int b:Int)) : Int (infix + a b)))");
    EXPECT_EQ(u("def curried(a: Int)(b: Int) = a * b"),
              "(unit (def curried ((a:Int) (b:Int)) (infix * a b)))");
    EXPECT_EQ(u("def sum(xs: Int*): Int = 0"), "(unit (def sum ((xs:Int*)) : Int (int 0)))");
    EXPECT_EQ(u("def id[A](x: A): A = x"), "(unit (def id [A] ((x:A)) : A x))");
    EXPECT_EQ(u("def pi = 3.14"), "(unit (def pi (float 3.14)))");
    EXPECT_EQ(u("def f() = 1"), "(unit (def f (()) (int 1)))");
    EXPECT_EQ(u("def f(x: => Int) = x"), "(unit (def f ((x:=> Int)) x))");
    EXPECT_EQ(u("def f(x: Int = 1) = x"), "(unit (def f ((x:Int = (int 1))) x))");
}

TEST(ParserDefs, MainAnnotationAndIndentedBody) {
    EXPECT_EQ(u("@main def hello(): Unit =\n  println(\"hi\")"),
              "(unit (def @main hello (()) : Unit (block (apply println (str \"hi\")))))");
}

TEST(ParserDefs, BraceAndIndentedBodiesAreEquivalent) {
    const std::string braces =
        u("def f(x: Int): Int = {\n  val y = x + 1\n  y * 2\n}");
    const std::string indented =
        u("def f(x: Int): Int =\n  val y = x + 1\n  y * 2");
    EXPECT_EQ(braces, indented);
    EXPECT_EQ(indented,
              "(unit (def f ((x:Int)) : Int (block (val y (infix + x (int 1))) "
              "(infix * y (int 2)))))");
}

TEST(ParserDefs, EndMarkers) {
    EXPECT_EQ(u("def f(x: Int): Int =\n  val y = x\n  y\nend f\nf(1)"),
              "(unit (def f ((x:Int)) : Int (block (val y x) y)) (apply f (int 1)))");
    EXPECT_EQ(u("def g =\n  if a then\n    b\n  else\n    c\n  end if\nend g"),
              "(unit (def g (block (if a (block b) (block c)))))");
    EXPECT_NE(unitError("def f =\n  1\nend g").find("misaligned end marker"),
              std::string::npos);
}

TEST(ParserDefs, LocalDefinitionsInBlocks) {
    EXPECT_EQ(e("{ val a = 1; def f(x: Int) = x + a; f(2) }"),
              "(block (val a (int 1)) (def f ((x:Int)) (infix + x a)) (apply f (int 2)))");
}

TEST(ParserDefs, ScriptModeTopLevelStatements) {
    EXPECT_EQ(u("println(1)\nprintln(2)"), "(unit (apply println (int 1)) (apply println (int 2)))");
    EXPECT_EQ(u("val a = 1; val b = 2"), "(unit (val a (int 1)) (val b (int 2)))");
}

TEST(ParserDefs, ModifiersAndOtherAnnotationsAreKeptOrIgnored) {
    EXPECT_EQ(u("private final def f = 1"), "(unit (def f (int 1)))");
    EXPECT_EQ(u("@tailrec def f(n: Int): Int = n"), "(unit (def @tailrec f ((n:Int)) : Int n))");
}

TEST(ParserDefs, Imports) {
    EXPECT_EQ(u("import scala.math.*"), "(unit (import scala.math.*))");
    EXPECT_EQ(u("import a.{b, c as d}"), "(unit (import a.{b, c as d}))");
}

TEST(ParserDefs, UnsupportedDefinitionsAreReportedClearly) {
    EXPECT_NE(unitError("class A").find("not implemented yet"), std::string::npos);
    EXPECT_NE(unitError("object O").find("not implemented yet"), std::string::npos);
    EXPECT_NE(unitError("val (a, b) = p").find("patterns in val definitions"), std::string::npos);
    EXPECT_NE(unitError("given x: Int = 1").find("(D3)"), std::string::npos);
    EXPECT_NE(unitError("def f(using x: Int) = x").find("(D3)"), std::string::npos);
    EXPECT_NE(unitError("def f() { 1 }").find("procedure syntax"), std::string::npos);
}

TEST(ParserDefs, IncompleteDefinitionsAskForMoreInput) {
    try {
        protoScala::parseSource("def f(x: Int) =");
        FAIL();
    } catch (const ParseError& err) {
        EXPECT_TRUE(err.atEof);
    }
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build_release && ctest --test-dir build_release -R ParserDefs --output-on-failure`
Expected: FAIL — `parseCompilationUnit` is unimplemented (link error or thrown "not implemented").

- [ ] **Step 3: Implement definitions**

```cpp
bool Parser::atDefinitionStart() const {
    switch (peek().kind) {
        case TokenKind::KwVal: case TokenKind::KwVar: case TokenKind::KwDef:
        case TokenKind::KwLazy: case TokenKind::KwImport: case TokenKind::At:
        case TokenKind::KwPrivate: case TokenKind::KwProtected: case TokenKind::KwFinal:
        case TokenKind::KwOverride: case TokenKind::KwAbstract: case TokenKind::KwSealed:
        case TokenKind::KwImplicit: case TokenKind::KwGiven: case TokenKind::KwClass:
        case TokenKind::KwObject: case TokenKind::KwTrait: case TokenKind::KwEnum:
        case TokenKind::KwCase: case TokenKind::KwType: case TokenKind::KwPackage:
        case TokenKind::KwExport:
            return true;
        case TokenKind::Identifier: {
            // Soft modifiers followed by a definition keyword.
            const std::string& s = peek().text;
            const bool soft = s == "open" || s == "inline" || s == "transparent" ||
                              s == "infix" || s == "opaque";
            const TokenKind n = peek(1).kind;
            return (soft && (n == TokenKind::KwDef || n == TokenKind::KwVal ||
                             n == TokenKind::KwVar || n == TokenKind::KwClass ||
                             n == TokenKind::KwTrait || n == TokenKind::KwObject ||
                             n == TokenKind::KwType || n == TokenKind::Identifier)) ||
                   s == "extension";
        }
        default:
            return false;
    }
}

NodePtr Parser::parseDefinition(std::vector<std::string> annotations) {
    // Annotations
    while (at(TokenKind::At)) {
        advance();
        annotations.push_back(expect(TokenKind::Identifier, "annotation name").text);
        if (at(TokenKind::LParen) && !peek().firstOnLine) {  // skip annotation arguments
            int depth = 0;
            do {
                if (at(TokenKind::LParen)) ++depth;
                if (at(TokenKind::RParen)) --depth;
                if (at(TokenKind::EndOfFile)) fail("')' expected", peek());
                advance();
            } while (depth > 0);
        }
        while (at(TokenKind::Newline)) advance();
    }
    // Modifiers (D5: advisory, ignored)
    bool isLazy = false;
    for (;;) {
        const Token& t = peek();
        if (t.kind == TokenKind::KwImplicit || t.kind == TokenKind::KwGiven)
            fail("implicits and givens are not supported (D3)", t);
        if (t.kind == TokenKind::KwLazy) { isLazy = true; advance(); continue; }
        const bool hardModifier =
            t.kind == TokenKind::KwPrivate || t.kind == TokenKind::KwProtected ||
            t.kind == TokenKind::KwFinal || t.kind == TokenKind::KwOverride ||
            t.kind == TokenKind::KwAbstract || t.kind == TokenKind::KwSealed;
        const bool softModifier =
            t.kind == TokenKind::Identifier &&
            (t.text == "open" || t.text == "inline" || t.text == "transparent" ||
             t.text == "infix" || t.text == "opaque") &&
            peek(1).kind != TokenKind::Equals && peek(1).kind != TokenKind::Newline;
        if (!hardModifier && !softModifier) break;
        advance();
        if ((t.kind == TokenKind::KwPrivate || t.kind == TokenKind::KwProtected) &&
            at(TokenKind::LBracket)) {  // private[pkg]
            advance();
            expect(TokenKind::Identifier, "qualifier");
            expect(TokenKind::RBracket, "']'");
        }
    }
    const Token& t = peek();
    switch (t.kind) {
        case TokenKind::KwVal: advance(); return parseValDef(t.pos, false, isLazy);
        case TokenKind::KwVar:
            if (isLazy) fail("lazy is not allowed on var", t);
            advance();
            return parseValDef(t.pos, true, false);
        case TokenKind::KwDef:
            if (isLazy) fail("lazy is not allowed on def", t);
            advance();
            return parseDefDef(t.pos, std::move(annotations));
        case TokenKind::KwImport: return parseImport();
        case TokenKind::KwClass: case TokenKind::KwObject: case TokenKind::KwTrait:
        case TokenKind::KwEnum: case TokenKind::KwCase: case TokenKind::KwType:
        case TokenKind::KwPackage: case TokenKind::KwExport:
            unsupported("'" + t.text + "' definitions", t);
        default:
            if (t.kind == TokenKind::Identifier && t.text == "extension")
                unsupported("extension methods", t);
            fail("definition expected but '" + t.text + "' found", t);
    }
}

NodePtr Parser::parseValDef(SourcePos pos, bool isVar, bool isLazy) {
    auto node = std::make_unique<ValDef>(pos);
    node->isVar = isVar;
    node->isLazy = isLazy;
    if (!at(TokenKind::Identifier) || peek().isOperator)
        unsupported("patterns in val definitions", peek());
    node->name = advance().text;
    if (at(TokenKind::Comma)) unsupported("patterns in val definitions", peek());
    if (at(TokenKind::Colon)) { advance(); node->type = parseType(); }
    if (!at(TokenKind::Equals)) {
        if (isVar || isLazy || !at(TokenKind::Equals))
            fail("'=' expected: a value definition needs an initialiser", peek());
    }
    advance();
    if (at(TokenKind::Underscore) && isVar)
        fail("default initialisation 'var x: T = _' is not supported", peek());
    node->rhs = parseExprOrIndented();
    return node;
}

std::vector<Param> Parser::parseParamClause() {
    expect(TokenKind::LParen, "'('");
    std::vector<Param> params;
    if (atIdent("using")) fail("implicits and givens are not supported (D3)", peek());
    if (at(TokenKind::KwImplicit)) fail("implicits and givens are not supported (D3)", peek());
    while (!at(TokenKind::RParen)) {
        Param p;
        p.pos = peek().pos;
        p.name = expect(TokenKind::Identifier, "parameter name").text;
        expect(TokenKind::Colon, "':' and a parameter type");
        if (at(TokenKind::Arrow)) { advance(); p.byName = true; }
        p.type = parseType();
        if (atIdent("*")) { advance(); p.repeated = true; }
        if (at(TokenKind::Equals)) { advance(); p.defaultValue = parseExpr(); }
        params.push_back(std::move(p));
        if (!at(TokenKind::Comma)) break;
        advance();
    }
    expect(TokenKind::RParen, "')'");
    return params;
}

NodePtr Parser::parseDefDef(SourcePos pos, std::vector<std::string> annotations) {
    auto node = std::make_unique<DefDef>(pos);
    node->annotations = std::move(annotations);
    node->name = expect(TokenKind::Identifier, "method name").text;
    if (at(TokenKind::LBracket)) {  // type parameters: names kept, bounds parsed and dropped
        advance();
        while (!at(TokenKind::RBracket)) {
            node->typeParams.push_back(expect(TokenKind::Identifier, "type parameter").text);
            while (at(TokenKind::Subtype) || at(TokenKind::Supertype) || at(TokenKind::Colon)) {
                advance();
                parseType();
            }
            if (!at(TokenKind::Comma)) break;
            advance();
        }
        expect(TokenKind::RBracket, "']'");
    }
    while (at(TokenKind::LParen) && !peek().firstOnLine) node->paramLists.push_back(parseParamClause());
    if (at(TokenKind::Colon)) { advance(); node->resultType = parseType(); }
    if (at(TokenKind::LBrace))
        fail("procedure syntax is not supported in Scala 3; write `def " + node->name +
             "(): Unit = ...`", peek());
    if (!at(TokenKind::Equals))
        fail("'=' expected: abstract methods are not supported outside classes", peek());
    advance();
    node->body = parseExprOrIndented();
    return node;
}
```

Also:

- `parseImport()`: consume `import`, then collect tokens up to the next `Newline`,
  `Semicolon`, `Outdent`, `RBrace` or EOF; build `text` by concatenating token texts with no
  spaces, except `", "` after a comma and `" as "` around the soft keyword `as` (so
  `import a.{b, c as d}` renders as in the test). `given` inside an import selector is
  accepted (parsed only, LANGUAGE §2).
- `parseBlockStat(terminator)` (replace the Task 3 version): `if (atDefinitionStart())
  return parseDefinition({});` then the Task 3 logic (block lambda, else `parseExpr()`).
- `parseCompilationUnit()`: `parseBlockBody(EndOfFile, {1,1})` without expecting a closing
  token, moving its statements into a `CompilationUnit`.
- `checkEndMarker(previous, marker)`: accept `if` after `If`, `while` after `While`, the
  definition's name after `DefDef`/`ValDef`, `val` after `ValDef`; otherwise
  `fail("misaligned end marker: 'end " + marker.text + "' does not close the preceding
  construct", marker)`.

- [ ] **Step 4: Run to verify pass**

Run: `cmake --build build_release && ctest --test-dir build_release -R "Parser" --output-on-failure`
Expected: all `Parser.*` and `ParserDefs.*` PASS.

- [ ] **Step 5: Commit**

```bash
git add src/frontend/Parser.cpp tests/unit/test_parser.cpp
git commit -m "frontend: definitions, @main, imports, end markers, compilation units

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```


---

### Task 5: Desugar (Phase 1 subset)

**Files:**
- Create: `src/frontend/Desugar.h`, `src/frontend/Desugar.cpp`, `tests/unit/test_desugar.cpp`
- Modify: `CMakeLists.txt` (add `Desugar.cpp`), `tests/unit/CMakeLists.txt` (add `test_desugar.cpp`)

**Interfaces:**
- Consumes: AST (Task 3), `parseSource`, `parseExpressionSource`, `isAssignmentOperator`, `isRightAssociative` (Tasks 3–4).
- Produces:
  - `void desugar(CompilationUnit& unit);` — in place.
  - `NodePtr desugarExpr(NodePtr e);` — for tests.
  - Postcondition ("core AST"), relied on by the compiler: no `Infix`, `Prefix`, `Parens` or `Typed` nodes remain; every `If` has an `elsep`; every `DefDef` has at most one parameter list.

Rules (DESIGN §3.4, the rows Phase 1 needs):

| Source | Core form |
|---|---|
| `a op b` | `a.op(b)` = `(apply (. a op) b)` |
| `a op: b` | `b.op:(a)`; when `a` is not an identifier or literal: `{ val <raN> = a; b.op:(<raN>) }` so `a` is evaluated first (Scala spec §6.12.3). `<raN>` cannot be written in source |
| `x op= y`, `x` an identifier | `x = x.op(y)` (Phase 1 has no `op=` members; see Open question Q19) |
| `e op= y`, `e` not an identifier | `e.op=(y)` |
| `-e`, `+e`, `!e`, `~e` | `e.unary_-` etc. = `(. e unary_-)` |
| `(e)`, `e: T` | `e` (types erased) |
| `if c then t` | `if c then t else ()` |
| `def f(a)(b)(c) = e` | `def f(a) = (b) => (c) => e` (each later list becomes a lambda; this also gives Scala 3's eta-expansion of `f(a)`) |

- [ ] **Step 1: Write the failing tests**

`tests/unit/test_desugar.cpp`:

```cpp
#include "frontend/AST.h"
#include "frontend/Desugar.h"
#include "frontend/Parser.h"

#include <gtest/gtest.h>

using namespace protoScala;

namespace {
std::string d(const std::string& src) { return dump(*desugarExpr(parseExpressionSource(src))); }
std::string du(const std::string& src) {
    auto unit = parseSource(src);
    desugar(*unit);
    return dump(*unit);
}
} // namespace

TEST(Desugar, InfixBecomesMethodCall) {
    EXPECT_EQ(d("1 + 2"), "(apply (. (int 1) +) (int 2))");
    EXPECT_EQ(d("a + b * c"), "(apply (. a +) (apply (. b *) c))");
    EXPECT_EQ(d("a max b"), "(apply (. a max) b)");
    EXPECT_EQ(d("a && b"), "(apply (. a &&) b)");
}

TEST(Desugar, RightAssociativeKeepsLeftToRightEvaluation) {
    EXPECT_EQ(d("a :: b"), "(apply (. b ::) a)");
    EXPECT_EQ(d("f(x) :: b"), "(block (val <ra0> (apply f x)) (apply (. b ::) <ra0>))");
}

TEST(Desugar, PrefixOperators) {
    EXPECT_EQ(d("-x"), "(. x unary_-)");
    EXPECT_EQ(d("!b"), "(. b unary_!)");
    EXPECT_EQ(d("-5"), "(int -5)");
}

TEST(Desugar, AssignmentOperators) {
    EXPECT_EQ(d("x += 1"), "(= x (apply (. x +) (int 1)))");
    EXPECT_EQ(d("a.b += 1"), "(apply (. (. a b) +=) (int 1))");
}

TEST(Desugar, ParensTypedAndIfWithoutElse) {
    EXPECT_EQ(d("(a)"), "a");
    EXPECT_EQ(d("x: Int"), "x");
    EXPECT_EQ(d("if a then b"), "(if a b ())");
}

TEST(Desugar, RecursesEverywhere) {
    EXPECT_EQ(d("x => -x + 1"), "(lambda (x) (apply (. (. x unary_-) +) (int 1)))");
    EXPECT_EQ(d("while (i < n) i += 1"),
              "(while (apply (. i <) n) (= i (apply (. i +) (int 1))))");
}

TEST(Desugar, MultipleParameterListsBecomeNestedLambdas) {
    EXPECT_EQ(du("def add(a: Int)(b: Int): Int = a + b"),
              "(unit (def add ((a:Int)) : Int (lambda (b:Int) (apply (. a +) b))))");
    EXPECT_EQ(du("def f(a: Int)(b: Int)(c: Int) = a"),
              "(unit (def f ((a:Int)) (lambda (b:Int) (lambda (c:Int) a))))");
}

TEST(Desugar, DefinitionsAndBlocks) {
    EXPECT_EQ(du("val x = -1 * y"), "(unit (val x (apply (. (int -1) *) y)))");
    EXPECT_EQ(du("def f = { val a = (1); a + 1 }"),
              "(unit (def f (block (val a (int 1)) (apply (. a +) (int 1)))))");
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build_release`
Expected: FAIL — `frontend/Desugar.h: No such file or directory`.

- [ ] **Step 3: Implement**

`Desugar.h`:

```cpp
/*
 * Desugar — rewrites the parser's AST into the core forms the compiler
 * understands (DESIGN §3.4, Phase 1 rows). Pure AST-to-AST; no protoCore.
 */
#pragma once
#include "frontend/AST.h"

namespace protoScala {

void desugar(CompilationUnit& unit);
NodePtr desugarExpr(NodePtr e);

} // namespace protoScala
```

`Desugar.cpp` (complete logic):

```cpp
#include "frontend/Desugar.h"
#include "frontend/Parser.h"

namespace protoScala {

namespace {

class Desugarer {
public:
    NodePtr expr(NodePtr n) {
        if (!n) return n;
        switch (n->kind) {
            case NodeKind::Infix:  return infix(std::move(n));
            case NodeKind::Prefix: {
                auto& p = as<Prefix>(*n);
                return std::make_unique<Select>(p.pos, expr(std::move(p.operand)),
                                                "unary_" + p.op);
            }
            case NodeKind::Parens: return expr(std::move(as<Parens>(*n).expr));
            case NodeKind::Typed:  return expr(std::move(as<Typed>(*n).expr));
            case NodeKind::If: {
                auto& i = as<If>(*n);
                i.cond = expr(std::move(i.cond));
                i.thenp = expr(std::move(i.thenp));
                i.elsep = i.elsep ? expr(std::move(i.elsep)) : std::make_unique<UnitLit>(i.pos);
                return n;
            }
            case NodeKind::While: {
                auto& w = as<While>(*n);
                w.cond = expr(std::move(w.cond));
                w.body = expr(std::move(w.body));
                return n;
            }
            case NodeKind::Select: {
                auto& s = as<Select>(*n);
                s.qualifier = expr(std::move(s.qualifier));
                return n;
            }
            case NodeKind::Apply: {
                auto& a = as<Apply>(*n);
                a.fn = expr(std::move(a.fn));
                for (auto& arg : a.args) arg = expr(std::move(arg));
                return n;
            }
            case NodeKind::TypeApply: {
                auto& t = as<TypeApply>(*n);
                t.fn = expr(std::move(t.fn));
                return n;
            }
            case NodeKind::Assign: {
                auto& a = as<Assign>(*n);
                a.target = expr(std::move(a.target));
                a.value = expr(std::move(a.value));
                return n;
            }
            case NodeKind::Return: {
                auto& r = as<Return>(*n);
                r.value = expr(std::move(r.value));
                return n;
            }
            case NodeKind::Block: {
                for (auto& s : as<Block>(*n).stats) s = expr(std::move(s));
                return n;
            }
            case NodeKind::Lambda: {
                auto& l = as<Lambda>(*n);
                params(l.params);
                l.body = expr(std::move(l.body));
                return n;
            }
            case NodeKind::Tuple:
                for (auto& e : as<Tuple>(*n).elems) e = expr(std::move(e));
                return n;
            case NodeKind::Splice: {
                auto& s = as<Splice>(*n);
                s.expr = expr(std::move(s.expr));
                return n;
            }
            case NodeKind::NamedArg: {
                auto& a = as<NamedArg>(*n);
                a.value = expr(std::move(a.value));
                return n;
            }
            case NodeKind::ValDef: {
                auto& v = as<ValDef>(*n);
                v.rhs = expr(std::move(v.rhs));
                return n;
            }
            case NodeKind::DefDef: return defDef(std::move(n));
            default:
                return n;  // literals, identifiers, imports, interpolations
        }
    }

private:
    int tempCounter_ = 0;

    void params(std::vector<Param>& ps) {
        for (auto& p : ps) p.defaultValue = expr(std::move(p.defaultValue));
    }

    static NodePtr call(SourcePos pos, NodePtr receiver, const std::string& name, NodePtr arg) {
        auto sel = std::make_unique<Select>(pos, std::move(receiver), name);
        auto app = std::make_unique<Apply>(pos, std::move(sel));
        app->args.push_back(std::move(arg));
        return app;
    }

    NodePtr infix(NodePtr n) {
        auto& in = as<Infix>(*n);
        const SourcePos pos = in.pos;
        NodePtr lhs = expr(std::move(in.lhs));
        NodePtr rhs = expr(std::move(in.rhs));
        const std::string op = in.op;
        if (isAssignmentOperator(op)) {
            const std::string base = op.substr(0, op.size() - 1);
            if (lhs->kind == NodeKind::Ident) {
                const auto& id = as<Ident>(*lhs);
                auto target = std::make_unique<Ident>(id.pos, id.name);
                auto value = call(pos, std::move(lhs), base, std::move(rhs));
                return std::make_unique<Assign>(pos, std::move(target), std::move(value));
            }
            return call(pos, std::move(lhs), op, std::move(rhs));
        }
        if (!isRightAssociative(op)) return call(pos, std::move(lhs), op, std::move(rhs));
        const NodeKind k = lhs->kind;
        const bool simple = k == NodeKind::Ident || k == NodeKind::IntLit ||
                            k == NodeKind::FloatLit || k == NodeKind::StringLit ||
                            k == NodeKind::CharLit || k == NodeKind::BoolLit ||
                            k == NodeKind::NullLit;
        if (simple) return call(pos, std::move(rhs), op, std::move(lhs));
        const std::string temp = "<ra" + std::to_string(tempCounter_++) + ">";
        auto block = std::make_unique<Block>(pos);
        auto val = std::make_unique<ValDef>(pos);
        val->name = temp;
        val->rhs = std::move(lhs);
        block->stats.push_back(std::move(val));
        block->stats.push_back(
            call(pos, std::move(rhs), op, std::make_unique<Ident>(pos, temp)));
        return block;
    }

    NodePtr defDef(NodePtr n) {
        auto& d = as<DefDef>(*n);
        for (auto& list : d.paramLists) params(list);
        NodePtr body = expr(std::move(d.body));
        // def f(a)(b)(c) = e  →  def f(a) = (b) => (c) => e
        while (d.paramLists.size() > 1) {
            auto lambda = std::make_unique<Lambda>(d.pos);
            lambda->params = std::move(d.paramLists.back());
            d.paramLists.pop_back();
            lambda->body = std::move(body);
            body = std::move(lambda);
        }
        d.body = std::move(body);
        return n;
    }
};

} // namespace

void desugar(CompilationUnit& unit) {
    Desugarer ds;
    for (auto& s : unit.stats) s = ds.expr(std::move(s));
}

NodePtr desugarExpr(NodePtr e) {
    Desugarer ds;
    return ds.expr(std::move(e));
}

} // namespace protoScala
```

Note: the `(val <ra0> ...)` dump prints the temp name as is; `Ident` of a temp dumps `<ra0>`.

- [ ] **Step 4: Run to verify pass**

Run: `cmake --build build_release && ctest --test-dir build_release -R Desugar --output-on-failure`
Expected: all `Desugar.*` PASS.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/frontend/Desugar.h src/frontend/Desugar.cpp tests/unit
git commit -m "frontend: desugar infix, prefix, op=, if-without-else, curried defs

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 6: Opcodes and BytecodeModule

**Files:**
- Create: `src/compiler/Opcodes.h`, `src/compiler/BytecodeModule.h`, `src/compiler/BytecodeModule.cpp`, `tests/unit/test_bytecode.cpp`
- Modify: `CMakeLists.txt` (add `protoscala_compiler`), `tests/unit/CMakeLists.txt`

**Interfaces:**
- Consumes: protoCore `ProtoString::createSymbol` (for `linkSymbols`).
- Produces:
  - `enum class Op : uint8_t` with the fixed numbering below, `using Instr = std::uint32_t`, `kOperandShift = 8`, `kMaxOperand = (1u << 24) - 1`, `kMaxExtendedOperand = (1ull << 48) - 1`, `const char* opName(Op)`.
  - `class BytecodeModule` (API below): `addInt`, `addBigInt`, `addDouble`, `addString`, `addChar`, `addSymbol`, `addSendSite`, `emit`, `emitJump`, `patchJumpTo`, `emitJumpBack`, `pos`, `code`, `lineAt`, `constAt`, `constCount`, metadata setters/getters, `addCapture`, `captureSpecs`, `captureCount`, `addBlock`, `block`, `blockCount`, `linkSymbols`, `disassemble`.

Instruction word: opcode in the low 8 bits, unsigned operand in the high 24 bits
(protoClojure `src/runtime/Opcodes.h:95-99`). An operand above `kMaxOperand` is emitted
as `EXTEND <operand >> 24>` followed by `<op> <operand & 0xFFFFFF>`; the engine combines
them (`(ext << 24) | low`), so operands reach 48 bits (DESIGN §3.5, protoST
`src/runtime/BytecodeModule.h:45-51` for the prefix idea). Jumps are never extended: a
jump offset above `kMaxOperand` throws `std::length_error("function body too large")`, so
forward jumps can be back-patched in place. Jump offsets count instruction words from the
instruction after the jump.

- [ ] **Step 1: Write the failing tests**

`tests/unit/test_bytecode.cpp`:

```cpp
#include "compiler/BytecodeModule.h"
#include "protoCore.h"

#include <gtest/gtest.h>

#include <cmath>

using protoScala::BytecodeModule;
using protoScala::Op;

TEST(Bytecode, ConstantPoolDeduplicatesByKind) {
    BytecodeModule m;
    const auto a = m.addInt(1);
    EXPECT_EQ(m.addInt(1), a);
    EXPECT_NE(m.addDouble(1.0), a);
    EXPECT_NE(m.addString("1"), a);
    EXPECT_EQ(m.addString("x"), m.addString("x"));
    EXPECT_NE(m.addDouble(0.0), m.addDouble(-0.0));  // by bit pattern
    EXPECT_EQ(m.addSymbol("println"), m.addSymbol("println"));
    EXPECT_NE(m.addSymbol("x"), m.addString("x"));
    EXPECT_EQ(m.addSendSite("+", 1), m.addSendSite("+", 1));
    EXPECT_NE(m.addSendSite("+", 1), m.addSendSite("+", 2));
    EXPECT_EQ(m.addBigInt("123456789012345678901234567890", 10),
              m.addBigInt("123456789012345678901234567890", 10));
    EXPECT_EQ(m.addChar(U'a'), m.addChar(U'a'));
}

TEST(Bytecode, EmitEncodesOpcodeOperandAndLine) {
    BytecodeModule m;
    const auto at = m.emit(Op::PUSH_CONST, 7, 3);
    EXPECT_EQ(at, 0u);
    EXPECT_EQ(m.code()[0] & 0xFF, static_cast<unsigned>(Op::PUSH_CONST));
    EXPECT_EQ(m.code()[0] >> 8, 7u);
    EXPECT_EQ(m.lineAt(0), 3);
}

TEST(Bytecode, WideOperandsUseExtend) {
    BytecodeModule m;
    const std::uint64_t wide = (1ull << 24) + 5;
    const auto at = m.emit(Op::PUSH_LOCAL, wide, 1);
    ASSERT_EQ(m.code().size(), 2u);
    EXPECT_EQ(at, 1u);  // position of the real instruction
    EXPECT_EQ(m.code()[0] & 0xFF, static_cast<unsigned>(Op::EXTEND));
    EXPECT_EQ(m.code()[0] >> 8, 1u);
    EXPECT_EQ(m.code()[1] >> 8, 5u);
    EXPECT_NE(m.disassemble().find("PUSH_LOCAL 16777221"), std::string::npos);
    EXPECT_THROW(m.emit(Op::PUSH_LOCAL, 1ull << 48, 1), std::length_error);
}

TEST(Bytecode, ForwardAndBackwardJumps) {
    BytecodeModule m;
    const auto top = m.pos();
    m.emit(Op::PUSH_TRUE, 0, 1);
    const auto j = m.emitJump(Op::JUMP_IF_FALSE, 1);
    m.emit(Op::PUSH_UNIT, 0, 1);
    m.emit(Op::POP, 0, 1);
    m.emitJumpBack(top, 1);
    m.patchJumpTo(j, m.pos());
    EXPECT_EQ(m.code()[j] >> 8, 3u);          // skips PUSH_UNIT, POP, JUMP_BACK
    EXPECT_EQ(m.code()[4] >> 8, 5u);          // back over 5 words to `top`
    EXPECT_EQ(m.code()[4] & 0xFF, static_cast<unsigned>(Op::JUMP_BACK));
}

TEST(Bytecode, MetadataCapturesAndBlocks) {
    BytecodeModule m;
    m.setName("f");
    m.setArity(2);
    m.setVariadic(true);
    m.setLocalCount(3);
    m.setMaxStack(4);
    m.addCapture(1, 5);
    auto sub = std::make_unique<BytecodeModule>();
    sub->setName("<lambda>");
    EXPECT_EQ(m.addBlock(std::move(sub)), 0u);
    EXPECT_EQ(m.block(0).name(), "<lambda>");
    EXPECT_EQ(m.captureCount(), 1);
    EXPECT_EQ(m.captureSpecs()[0].parentSlot, 1);
    EXPECT_EQ(m.captureSpecs()[0].localSlot, 5);
    EXPECT_EQ(m.arity(), 2);
    EXPECT_TRUE(m.isVariadic());
}

TEST(Bytecode, DisassemblyNamesConstants) {
    BytecodeModule m;
    m.setName("<top>");
    m.emit(Op::PUSH_GLOBAL, m.addSymbol("println"), 1);
    m.emit(Op::PUSH_CONST, m.addString("hi"), 1);
    m.emit(Op::CALL, 1, 1);
    m.emit(Op::RETURN, 0, 2);
    const std::string text = m.disassemble();
    EXPECT_NE(text.find("function <top> arity=0"), std::string::npos);
    EXPECT_NE(text.find("PUSH_GLOBAL 0 ; println"), std::string::npos);
    EXPECT_NE(text.find("PUSH_CONST 1 ; \"hi\""), std::string::npos);
    EXPECT_NE(text.find("L2  RETURN"), std::string::npos);
}

TEST(Bytecode, LinkSymbolsInternsInTheSpace) {
    proto::ProtoSpace space;
    proto::ProtoContext* ctx = space.rootContext;
    BytecodeModule m;
    const auto s = m.addSymbol("answer");
    const auto site = m.addSendSite("length", 0);
    auto sub = std::make_unique<BytecodeModule>();
    const auto inner = sub->addSymbol("inner");
    m.addBlock(std::move(sub));
    m.linkSymbols(ctx);
    EXPECT_EQ(m.constAt(s).symbol, proto::ProtoString::createSymbol(ctx, "answer"));
    EXPECT_EQ(m.constAt(site).symbol, proto::ProtoString::createSymbol(ctx, "length"));
    EXPECT_EQ(m.block(0).constAt(inner).symbol, proto::ProtoString::createSymbol(ctx, "inner"));
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build_release`
Expected: FAIL — `compiler/BytecodeModule.h: No such file or directory`.

- [ ] **Step 3: Write `Opcodes.h` (complete; the numbering is fixed from here on)**

```cpp
/*
 * Opcodes — the protoScala instruction set (DESIGN §3.5).
 *
 * One 32-bit word per instruction: opcode in the low 8 bits, unsigned 24-bit
 * operand in the high bits. EXTEND supplies bits 24..47 of the next
 * instruction's operand. The numbering is fixed; later phases only append in
 * their reserved ranges, and docs/STATUS.md mirrors this table.
 *
 * Stack effects are written as [before] -> [after], top of stack on the right.
 */
#pragma once
#include <cstdint>

namespace protoScala {

enum class Op : uint8_t {
    NOP          = 0,
    EXTEND       = 1,   // operand: high 24 bits of the next instruction's operand
    // Constants and stack
    PUSH_CONST   = 2,   // [] -> [consts[operand]]
    PUSH_UNIT    = 3,   // [] -> [()]
    PUSH_NULL    = 4,   // [] -> [null]
    PUSH_TRUE    = 5,
    PUSH_FALSE   = 6,
    POP          = 7,   // [v] -> []
    DUP          = 8,   // [v] -> [v v]
    // Locals and boxed cells (captured vars and local defs)
    PUSH_LOCAL   = 9,   // [] -> [slot[operand]]
    STORE_LOCAL  = 10,  // [v] -> []            slot[operand] = v
    MAKE_CELL    = 11,  // [] -> []             slot[operand] = new Cell(null)
    PUSH_CELL    = 12,  // [] -> [slot[operand].value]
    STORE_CELL   = 13,  // [v] -> []            slot[operand].value = v
    // Globals (operand: a Symbol constant)
    PUSH_GLOBAL  = 14,  // [] -> [globals.name]
    STORE_GLOBAL = 15,  // [v] -> []
    // Functions and calls
    MAKE_FN      = 16,  // [c1..cn] -> [fn]     operand: block index; n = its captureCount
    CALL         = 17,  // [f a1..an] -> [r]    operand: n
    CALL_SPREAD  = 18,  // [f a1..an list] -> [r]  operand: n; list elements follow a1..an
    SEND         = 19,  // [recv a1..an] -> [r] operand: SendSite constant (name, n)
    RETURN       = 20,  // [v] -> returns v
    MAKE_LAZY    = 21,  // [thunk] -> [lazy]
    FORCE        = 22,  // [v] -> [forced v]    evaluates a lazy once; other values pass
    // Control flow (offsets in words, from the next instruction)
    JUMP          = 23,
    JUMP_IF_FALSE = 24, // [b] -> []            b must be a Boolean
    JUMP_IF_TRUE  = 25, // [b] -> []
    JUMP_BACK     = 26, // backward; also a GC safepoint (Open question Q21)
    // SmallInteger fast paths with protoCore fallback (DESIGN §3.6)
    ADD = 27, SUB = 28, MUL = 29,          // [a b] -> [r]
    LT = 30, LE = 31, GT = 32, GE = 33,    // [a b] -> [Boolean]
    EQ = 34, NE = 35,                      // Scala == and !=
    NEG = 36,                              // [a] -> [-a]
    NOT = 37,                              // [b] -> [!b]
    // 38..63   reserved (Phase 1 additions)
    // 64..95   object model, Phase 2: SEND_SUPER, TEST_TYPE, TEST_PROTO, UNAPPLY_FIELDS
    // 96..127  exceptions, Phase 4: THROW (+ per-module handler table)
    // 128..159 actors, Phase 5: SEND_ASYNC, ASK, AWAIT
};

using Instr = std::uint32_t;
inline constexpr unsigned      kOperandShift       = 8;
inline constexpr std::uint32_t kMaxOperand         = (1u << 24) - 1;
inline constexpr std::uint64_t kMaxExtendedOperand = (1ull << 48) - 1;

const char* opName(Op op);

} // namespace protoScala
```

- [ ] **Step 4: Write `BytecodeModule.h`**

```cpp
/*
 * BytecodeModule — one compiled function body (DESIGN §3.5): code words and a
 * parallel source-line table, a constant pool de-duplicated by kind, nested
 * function bodies (blocks), capture specs and frame metadata.
 *
 * Ownership: a module and its blocks are C++ objects owned by the session
 * (std::unique_ptr). A function object points at its module through a
 * SmallInteger holding the address (MAKE_FN), exactly as protoClojure's
 * wrappers do (src/runtime/ExecutionEngine.cpp:895-910); the session keeps
 * every module alive until the ProtoSpace is destroyed.
 *
 * P1 boundary: the only ProtoObject pointers a module holds are interned
 * symbols (Const::symbol), filled by linkSymbols. createSymbol symbols are
 * strong and never collected, so no GC reference from here is needed; a
 * module therefore runs only in the ProtoSpace it was linked against.
 */
#pragma once
#include "compiler/Opcodes.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace proto {
class ProtoContext;
class ProtoString;
}

namespace protoScala {

class BytecodeModule {
public:
    enum class ConstKind : uint8_t { Int, BigInt, Double, String, Char, Symbol, SendSite };

    struct Const {
        ConstKind kind;
        long long ival = 0;         // Int; Char (code point)
        double dval = 0.0;          // Double
        std::string sval;           // String bytes; BigInt digits; Symbol/SendSite name
        int base = 10;              // BigInt
        std::uint32_t argc = 0;     // SendSite
        const proto::ProtoString* symbol = nullptr;  // Symbol/SendSite, after linkSymbols
    };

    struct CaptureSpec {
        int parentSlot;  // slot read in the enclosing frame at MAKE_FN
        int localSlot;   // slot written in this frame on entry
    };

    std::size_t addInt(long long v);
    std::size_t addBigInt(const std::string& digits, int base);
    std::size_t addDouble(double v);
    std::size_t addString(const std::string& s);
    std::size_t addChar(char32_t c);
    std::size_t addSymbol(const std::string& name);
    std::size_t addSendSite(const std::string& name, std::uint32_t argc);

    // Emits `op operand` (with an EXTEND prefix when operand > kMaxOperand) and
    // returns the position of the `op` word. Throws std::length_error beyond
    // kMaxExtendedOperand.
    std::size_t emit(Op op, std::uint64_t operand, int line);
    // A forward jump with a placeholder operand; never extended.
    std::size_t emitJump(Op op, int line);
    // Sets the jump at `jumpAt` to land on `target` (target >= jumpAt + 1).
    void patchJumpTo(std::size_t jumpAt, std::size_t target);
    // JUMP_BACK to `target` (an earlier position).
    std::size_t emitJumpBack(std::size_t target, int line);

    std::size_t pos() const { return code_.size(); }
    const std::vector<Instr>& code() const { return code_; }
    int lineAt(std::size_t pc) const { return pc < lines_.size() ? lines_[pc] : 0; }
    const Const& constAt(std::size_t i) const { return consts_[i]; }
    std::size_t constCount() const { return consts_.size(); }

    const std::string& name() const { return name_; }
    void setName(std::string n) { name_ = std::move(n); }
    int arity() const { return arity_; }
    void setArity(int n) { arity_ = n; }
    bool isVariadic() const { return variadic_; }
    void setVariadic(bool v) { variadic_ = v; }
    int localCount() const { return localCount_; }
    void setLocalCount(int n) { localCount_ = n; }
    int maxStack() const { return maxStack_; }
    void setMaxStack(int n) { maxStack_ = n; }

    void addCapture(int parentSlot, int localSlot) { captures_.push_back({parentSlot, localSlot}); }
    const std::vector<CaptureSpec>& captureSpecs() const { return captures_; }
    int captureCount() const { return static_cast<int>(captures_.size()); }

    std::size_t addBlock(std::unique_ptr<BytecodeModule> sub);
    const BytecodeModule& block(std::size_t i) const { return *blocks_[i]; }
    std::size_t blockCount() const { return blocks_.size(); }

    // Interns every Symbol and SendSite name, recursively through blocks.
    void linkSymbols(proto::ProtoContext* ctx);

    // Human-readable listing of this module and its blocks (tests, --disassemble).
    std::string disassemble() const;

private:
    std::vector<Instr> code_;
    std::vector<int> lines_;  // one entry per code word
    std::vector<Const> consts_;
    std::unordered_map<long long, std::size_t> intIndex_;
    std::unordered_map<std::uint64_t, std::size_t> doubleIndex_;  // by bit pattern
    std::unordered_map<std::string, std::size_t> bigIntIndex_;    // base + ":" + digits
    std::unordered_map<std::string, std::size_t> stringIndex_;
    std::unordered_map<long long, std::size_t> charIndex_;
    std::unordered_map<std::string, std::size_t> symbolIndex_;
    std::unordered_map<std::string, std::size_t> sendIndex_;      // name + "/" + argc
    std::vector<std::unique_ptr<BytecodeModule>> blocks_;
    std::vector<CaptureSpec> captures_;
    std::string name_ = "<top>";
    int arity_ = 0;
    bool variadic_ = false;
    int localCount_ = 0;
    int maxStack_ = 0;
};

} // namespace protoScala
```

- [ ] **Step 5: Write `BytecodeModule.cpp`**

`findOrAdd` and the double-by-bit-pattern rule are copied from protoClojure
`src/runtime/BytecodeModule.cpp:349-392`. The encoding functions:

```cpp
std::size_t BytecodeModule::emit(Op op, std::uint64_t operand, int line) {
    if (operand > kMaxExtendedOperand)
        throw std::length_error(std::string(opName(op)) + " operand " +
                                std::to_string(operand) + " exceeds the bytecode limit");
    if (operand > kMaxOperand) {
        code_.push_back(static_cast<Instr>((operand >> 24) << kOperandShift) |
                        static_cast<Instr>(Op::EXTEND));
        lines_.push_back(line);
        operand &= kMaxOperand;
    }
    code_.push_back((static_cast<Instr>(operand) << kOperandShift) | static_cast<Instr>(op));
    lines_.push_back(line);
    return code_.size() - 1;
}

std::size_t BytecodeModule::emitJump(Op op, int line) { return emit(op, 0, line); }

void BytecodeModule::patchJumpTo(std::size_t jumpAt, std::size_t target) {
    if (jumpAt >= code_.size() || target <= jumpAt)
        throw std::out_of_range("patchJumpTo: bad jump or target");
    const std::size_t offset = target - (jumpAt + 1);
    if (offset > kMaxOperand) throw std::length_error("function body too large");
    const Instr op = code_[jumpAt] & 0xFF;
    code_[jumpAt] = (static_cast<Instr>(offset) << kOperandShift) | op;
}

std::size_t BytecodeModule::emitJumpBack(std::size_t target, int line) {
    const std::size_t offset = (code_.size() + 1) - target;
    if (offset > kMaxOperand) throw std::length_error("function body too large");
    return emit(Op::JUMP_BACK, offset, line);
}

void BytecodeModule::linkSymbols(proto::ProtoContext* ctx) {
    for (Const& c : consts_)
        if (c.kind == ConstKind::Symbol || c.kind == ConstKind::SendSite)
            c.symbol = proto::ProtoString::createSymbol(ctx, c.sval);
    for (auto& b : blocks_) b->linkSymbols(ctx);
}
```

`disassemble()` format (tests rely on it):

```
function <name> arity=<n>[ variadic] locals=<n> stack=<n> captures=<n>
  0000  L<line>  <OPNAME> <operand>[ ; <comment>]
```

An `EXTEND` word is folded into the following instruction's printed operand (and not
printed itself); comments: `PUSH_CONST` shows the constant (`"hi"` quoted and escaped,
`'c'`, integers, doubles via `%.17g`, big integers as digits), `PUSH_GLOBAL`/`STORE_GLOBAL`
the symbol name, `SEND` `name/argc`, `MAKE_FN` `-> block <i>`, jumps `-> <target pc>`.
Blocks are appended after the parent, each preceded by a blank line and indented by two
more spaces per nesting level. `opName` is a `switch` over every `Op` with no `default:`.

`CMakeLists.txt`:

```cmake
# --- Compiler: opcodes, bytecode modules, AST -> bytecode ------------------------
add_library(protoscala_compiler STATIC
    src/compiler/BytecodeModule.cpp
)
target_link_libraries(protoscala_compiler PUBLIC protoscala_frontend protoscala_support)
target_compile_options(protoscala_compiler PRIVATE -Wall -Wextra -Wpedantic)
```

and add `test_bytecode.cpp` plus `protoscala_compiler` to the unit-test target.

- [ ] **Step 6: Run to verify pass**

Run: `cmake --build build_release && ctest --test-dir build_release -R Bytecode --output-on-failure`
Expected: all `Bytecode.*` PASS.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt src/compiler tests/unit
git commit -m "compiler: Phase 1 opcode set and BytecodeModule (EXTEND, pool, lines)

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```


---

### Task 7: Compiler

**Files:**
- Create: `src/compiler/GlobalTable.h`, `src/compiler/Compiler.h`, `src/compiler/Compiler.cpp`, `tests/unit/test_compiler.cpp`
- Modify: `CMakeLists.txt` (add `Compiler.cpp` to `protoscala_compiler`), `tests/unit/CMakeLists.txt`

**Interfaces:**
- Consumes: core AST (Task 5 postcondition), `BytecodeModule`, `Op` (Task 6).
- Produces:
  - `enum class BindingKind : uint8_t { Val, Var, LazyVal, Def, ParamlessDef, Builtin, Param };`
  - `class GlobalTable { void declare(const std::string&, BindingKind); std::optional<BindingKind> find(const std::string&) const; }`
  - `struct CompileError : std::runtime_error { SourcePos pos; }`
  - `enum class UnitMode { Script, Repl };`
  - `struct CompiledUnit { std::unique_ptr<BytecodeModule> module; std::string mainName; bool mainTakesArgs; std::string resultName; std::vector<std::string> definitions; };`
  - `class Compiler { Compiler(GlobalTable& globals); CompiledUnit compileUnit(const CompilationUnit& unit, UnitMode mode, int replResultIndex = 0); }`

Semantics the compiler implements:

1. **Top-level code** is one arity-0 module. It first defines every top-level `def`
   (`MAKE_FN` + `STORE_GLOBAL`, so defs may be referenced before their text), then runs the
   `val`/`var`/`lazy val` initialisers and expression statements in source order (Open
   question Q2), then `PUSH_UNIT; RETURN`. Top-level names are **globals** (attributes of the
   module globals object); every top-level name is declared in the `GlobalTable` before any
   code is compiled. In `Repl` mode, when the last statement is an expression its value is
   stored in global `res<N>` (`resultName`), and `definitions` lists `val x`, `var x`,
   `lazy val x`, `def f` for the echo.
2. **@main**: at most one top-level `def` annotated `@main` (else "only one @main method is
   allowed per file"); its parameter lists must be `()` or a single repeated parameter
   (`args: String*`, which sets `mainTakesArgs`), else "@main methods take no parameters or
   a single repeated String parameter" (Open question Q7). The session calls it after the
   top-level code.
3. **Name resolution**: innermost block scope outwards within the current function, then
   enclosing functions (creating a capture chain through every intermediate function, as
   protoClojure's `resolveLocal`, `src/compiler/Compiler.h:137-143`), then the
   `GlobalTable`, else `CompileError("Not found: <name>")`.
4. **Captures are per activation**: `MAKE_FN` copies the captured slot values each time it
   runs. A captured `var`, a local `def`, and a local `val`/`var` referenced from inside a
   local `def` body are **boxed**: their slot holds a Cell (`MAKE_CELL` at the start of the
   enclosing block), so all closures share the variable (Scala `var` semantics) and local
   defs can be mutually recursive and hoisted (Open question Q4). A `CaptureAnalysis`
   pre-pass over each function decides which declarations are boxed.
5. **Blocks**: all local `def`s of a block are hoisted (their `MAKE_FN` runs at block entry,
   after the `MAKE_CELL`s), other statements run in order; the block's value is its last
   statement if that is an expression, else `()`.
6. **References**: a `ParamlessDef` reference compiles to `CALL 0`; a `LazyVal` reference
   to `FORCE`; a `Def` reference is the function value (eta-expansion, Open question Q3).
7. **Calls**: `Apply(Select(recv, op), [arg])` with `op` in `+ - * < <= > >= == !=` → the
   fast opcode; `&&`/`||` → short-circuit jumps; `Select(e, "unary_-")` → `NEG`,
   `Select(e, "unary_!")` → `NOT`; any other `Apply(Select(recv, m), args)` → `SEND m/n`; a
   bare `Select(recv, m)` → `SEND m/0`; `Apply(f, args)` otherwise → `CALL n` (or
   `CALL_SPREAD n` when the last argument is a `Splice`).
8. **Assignment**: to a local `var` (`STORE_LOCAL`/`STORE_CELL`) or global `var`
   (`STORE_GLOBAL`); the expression's value is `()`. Anything else is
   `CompileError("Reassignment to val <name>")`; a `Select`/`Apply` target is
   "assignment to fields and indexed elements is not implemented yet".
9. **Not yet supported, rejected with CompileError**: `return` outside a `def` body or
   inside a lambda ("return inside a lambda is not supported", Open question Q5), tuples,
   string interpolation, named arguments, default parameter values, by-name parameters
   (Open question Q6), splices outside a function call (`recv.m(xs*)`).
10. **Stack depth**: the compiler tracks the operand-stack depth of every emitted
    instruction and records `maxStack`; the VM sizes each frame exactly (no growth checks).

- [ ] **Step 1: Write the failing tests**

`tests/unit/test_compiler.cpp`:

```cpp
#include "compiler/Compiler.h"
#include "frontend/Desugar.h"
#include "frontend/Parser.h"

#include <gtest/gtest.h>

using namespace protoScala;

namespace {

CompiledUnit compile(const std::string& src, GlobalTable& g, UnitMode mode = UnitMode::Script) {
    auto unit = parseSource(src);
    desugar(*unit);
    Compiler c(g);
    return c.compileUnit(*unit, mode, 0);
}

std::string listing(const std::string& src, UnitMode mode = UnitMode::Script) {
    GlobalTable g;
    g.declare("println", BindingKind::Builtin);
    return compile(src, g, mode).module->disassemble();
}

std::string compileError(const std::string& src) {
    GlobalTable g;
    g.declare("println", BindingKind::Builtin);
    try {
        compile(src, g);
    } catch (const CompileError& e) {
        return e.what();
    }
    return "";
}

bool has(const std::string& text, const std::string& piece) {
    return text.find(piece) != std::string::npos;
}

} // namespace

TEST(Compiler, ArithmeticUsesFastPaths) {
    const auto l = listing("val x = 1 + 2 * 3");
    EXPECT_TRUE(has(l, "MUL"));
    EXPECT_TRUE(has(l, "ADD"));
    EXPECT_TRUE(has(l, "STORE_GLOBAL"));
    EXPECT_FALSE(has(l, "SEND"));
}

TEST(Compiler, OtherOperatorsAndMethodsAreSends) {
    const auto l = listing("val x = 7 / 2\nval n = \"abc\".length\nval m = 3.max(4)");
    EXPECT_TRUE(has(l, "; //1"));
    EXPECT_TRUE(has(l, "; length/0"));
    EXPECT_TRUE(has(l, "; max/1"));
}

TEST(Compiler, MainIsRecordedAndDefsAreHoisted) {
    GlobalTable g;
    g.declare("println", BindingKind::Builtin);
    auto cu = compile("@main def m(): Unit = helper()\ndef helper() = println(\"hi\")", g);
    EXPECT_EQ(cu.mainName, "m");
    EXPECT_FALSE(cu.mainTakesArgs);
    const auto l = cu.module->disassemble();
    // Both MAKE_FN come before anything else in the top-level code.
    EXPECT_LT(l.find("MAKE_FN"), l.find("PUSH_UNIT"));
    EXPECT_TRUE(has(l, "CALL 1"));
    EXPECT_EQ(g.find("helper"), BindingKind::Def);
}

TEST(Compiler, MainWithStringVarargs) {
    GlobalTable g;
    auto cu = compile("@main def run(args: String*): Unit = ()", g);
    EXPECT_TRUE(cu.mainTakesArgs);
}

TEST(Compiler, MainRestrictions) {
    EXPECT_TRUE(has(compileError("@main def a() = 1\n@main def b() = 2"), "only one @main"));
    EXPECT_TRUE(has(compileError("@main def a(x: Int) = x"), "@main methods take"));
}

TEST(Compiler, CapturedVarIsBoxed) {
    const auto l = listing("def f() = { var c = 0; () => { c += 1; c } }");
    EXPECT_TRUE(has(l, "MAKE_CELL"));
    EXPECT_TRUE(has(l, "STORE_CELL"));
    EXPECT_TRUE(has(l, "PUSH_CELL"));
}

TEST(Compiler, CapturedValIsCopied) {
    const auto l = listing("def f(x: Int) = { val y = x; () => y }");
    EXPECT_FALSE(has(l, "MAKE_CELL"));
    EXPECT_TRUE(has(l, "captures=1"));
}

TEST(Compiler, LocalDefsAreBoxedAndHoisted) {
    const auto l = listing(
        "def f(n: Int): Boolean = {\n"
        "  def even(k: Int): Boolean = if k == 0 then true else odd(k - 1)\n"
        "  def odd(k: Int): Boolean = if k == 0 then false else even(k - 1)\n"
        "  even(n)\n"
        "}");
    EXPECT_TRUE(has(l, "MAKE_CELL"));
}

TEST(Compiler, ParamlessDefReferenceIsACall) {
    const auto l = listing("def pi = 3.14\nval x = pi");
    EXPECT_TRUE(has(l, "CALL 0"));
}

TEST(Compiler, LazyValUsesMakeLazyAndForce) {
    const auto l = listing("lazy val z = 1 + 1\nval w = z");
    EXPECT_TRUE(has(l, "MAKE_LAZY"));
    EXPECT_TRUE(has(l, "FORCE"));
}

TEST(Compiler, ShortCircuitBooleans) {
    const auto l = listing("val a = true\nval b = a && false || a");
    EXPECT_TRUE(has(l, "JUMP_IF_FALSE"));
    EXPECT_TRUE(has(l, "JUMP_IF_TRUE"));
    EXPECT_FALSE(has(l, "; &&/1"));
}

TEST(Compiler, WhileLoopsJumpBack) {
    const auto l = listing("var i = 0\nwhile i < 3 do i += 1");
    EXPECT_TRUE(has(l, "JUMP_BACK"));
}

TEST(Compiler, SpliceUsesCallSpread) {
    const auto l = listing("def f(xs: Int*) = xs\ndef g(ys: Int*) = f(ys*)");
    EXPECT_TRUE(has(l, "CALL_SPREAD 0"));
    EXPECT_TRUE(has(l, "arity=1 variadic"));
}

TEST(Compiler, MaxStackCoversTheDeepestCall) {
    GlobalTable g;
    g.declare("println", BindingKind::Builtin);
    auto cu = compile("def f(a: Int, b: Int, c: Int) = a\nval x = f(1, 2, 3)", g);
    EXPECT_GE(cu.module->maxStack(), 4);
}

TEST(Compiler, ReplModeBindsResult) {
    GlobalTable g;
    auto cu = compile("val a = 1\na + 1", g, UnitMode::Repl);
    EXPECT_EQ(cu.resultName, "res0");
    ASSERT_EQ(cu.definitions.size(), 1u);
    EXPECT_EQ(cu.definitions[0], "val a");
    EXPECT_EQ(g.find("res0"), BindingKind::Val);
}

TEST(Compiler, SemanticErrors) {
    EXPECT_TRUE(has(compileError("val a = 1\na = 2"), "Reassignment to val a"));
    EXPECT_TRUE(has(compileError("val b = zz"), "Not found: zz"));
    EXPECT_TRUE(has(compileError("val f = () => return 1"), "return inside a lambda"));
    EXPECT_TRUE(has(compileError("return 1"), "return outside"));
    EXPECT_TRUE(has(compileError("val t = (1, 2)"), "tuples are not implemented yet"));
    EXPECT_TRUE(has(compileError("val s = s\"x\""), "string interpolation is not implemented yet"));
    EXPECT_TRUE(has(compileError("def f(x: Int) = x\nval y = f(x = 1)"), "named arguments"));
    EXPECT_TRUE(has(compileError("def f(x: Int = 1) = x"), "default parameter values"));
    EXPECT_TRUE(has(compileError("def f(x: => Int) = x"), "by-name parameters"));
}
```

(The tests read the disassembly format of Task 6; a send-site comment is `name/argc`, so
`7 / 2` shows `; //1`.)

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build_release`
Expected: FAIL — `compiler/Compiler.h: No such file or directory`.

- [ ] **Step 3: Write `GlobalTable.h` and `Compiler.h`**

```cpp
// GlobalTable.h — compile-time view of module / session globals.
#pragma once
#include <optional>
#include <string>
#include <unordered_map>

namespace protoScala {

enum class BindingKind : uint8_t { Val, Var, LazyVal, Def, ParamlessDef, Builtin, Param };

class GlobalTable {
public:
    // Declares or redeclares (REPL redefinition) a global.
    void declare(const std::string& name, BindingKind kind) { table_[name] = kind; }
    std::optional<BindingKind> find(const std::string& name) const {
        auto it = table_.find(name);
        if (it == table_.end()) return std::nullopt;
        return it->second;
    }
private:
    std::unordered_map<std::string, BindingKind> table_;
};

} // namespace protoScala
```

```cpp
/*
 * Compiler — core AST (after Desugar) to BytecodeModule (DESIGN §3.5).
 *
 * Pure C++: it never touches protoCore. Constants are recorded in the module
 * pools and materialised by the VM; symbol names are interned later by
 * BytecodeModule::linkSymbols. Per-function state lives in FunctionState
 * objects on the C++ stack (one per nested function being compiled), and
 * block scopes are a std::deque so references to outer scopes survive
 * pushes (protoST D29).
 */
#pragma once
#include "compiler/BytecodeModule.h"
#include "compiler/GlobalTable.h"
#include "frontend/AST.h"

#include <deque>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace protoScala {

struct CompileError : std::runtime_error {
    CompileError(const std::string& msg, SourcePos p) : std::runtime_error(msg), pos(p) {}
    SourcePos pos;
};

enum class UnitMode { Script, Repl };

struct CompiledUnit {
    std::unique_ptr<BytecodeModule> module;  // arity 0: the top-level code
    std::string mainName;                    // empty when the unit has no @main
    bool mainTakesArgs = false;              // @main def f(args: String*)
    std::string resultName;                  // Repl: "res<N>" or empty
    std::vector<std::string> definitions;    // Repl echo: "val x", "def f", ...
};

class Compiler {
public:
    explicit Compiler(GlobalTable& globals) : globals_(globals) {}

    CompiledUnit compileUnit(const CompilationUnit& unit, UnitMode mode,
                             int replResultIndex = 0);

private:
    struct LocalInfo {
        int slot = 0;
        BindingKind kind = BindingKind::Val;
        bool boxed = false;
        bool captured = false;  // reached through a capture of an enclosing function
    };
    struct FunctionState {
        BytecodeModule* mod = nullptr;
        FunctionState* parent = nullptr;
        std::deque<std::unordered_map<std::string, LocalInfo>> scopes;
        int nextSlot = 0;
        int depth = 0;
        int maxDepth = 0;
        bool allowsReturn = false;  // def bodies
        bool isTopLevel = false;
    };
    enum class RefKind { Local, Global };
    struct Resolution {
        RefKind ref;
        LocalInfo local;    // RefKind::Local
        BindingKind kind;   // binding kind in both cases
    };

    GlobalTable& globals_;
    FunctionState* fn_ = nullptr;
    std::unordered_set<const Node*> boxed_;  // declarations kept in Cells

    // Emission with stack-depth accounting.
    void emit(Op op, std::uint64_t operand, SourcePos pos, int stackEffect);
    std::size_t emitJump(Op op, SourcePos pos, int stackEffect);
    void adjust(int delta);

    // Scopes and names.
    int newSlot() { return fn_->nextSlot++; }
    LocalInfo declareLocal(const std::string& name, BindingKind kind, bool boxed);
    Resolution resolve(const std::string& name, SourcePos pos);
    const LocalInfo* findInFunction(FunctionState* f, const std::string& name);
    LocalInfo captureInto(FunctionState* f, const std::string& name, SourcePos pos, bool* found);

    // Code generation.
    void compileExpr(const Node& n);
    void compileStatement(const Node& n, bool keepValue);
    void compileIdent(const Ident& id);
    void compileApply(const Apply& a);
    void compileSelect(const Select& s);
    void compileAssign(const Assign& a);
    void compileIf(const If& i);
    void compileWhile(const While& w);
    void compileBlock(const Block& b);
    void compileReturn(const Return& r);
    void compileShortCircuit(const Node& lhs, const Node& rhs, bool isAnd, SourcePos pos);
    void compileArgsAndCall(const std::vector<NodePtr>& args, SourcePos pos);
    // Compiles a function body into a new block of the current module and emits
    // the capture pushes and MAKE_FN. `isDef` enables `return`.
    void compileFunction(const std::string& name, const std::vector<Param>& params,
                         const Node& body, bool isDef, SourcePos pos);
    void compileLazyThunk(const Node& rhs, SourcePos pos);  // thunk + MAKE_LAZY
    void storeLocal(const LocalInfo& info, SourcePos pos);
    void loadLocal(const LocalInfo& info, SourcePos pos);

    // Pre-pass: fills boxed_ for the declarations of one function body.
    void analyseCaptures(const std::vector<Param>& params, const Node& body);
};

} // namespace protoScala
```

- [ ] **Step 4: Write `Compiler.cpp` — the essential algorithms**

Emission, resolution and captures:

```cpp
#include "compiler/Compiler.h"

namespace protoScala {

namespace {
int line(SourcePos p) { return p.line; }

bool isFastBinary(const std::string& op, Op* out) {
    static const std::pair<const char*, Op> table[] = {
        {"+", Op::ADD}, {"-", Op::SUB}, {"*", Op::MUL}, {"<", Op::LT}, {"<=", Op::LE},
        {">", Op::GT}, {">=", Op::GE}, {"==", Op::EQ}, {"!=", Op::NE}};
    for (const auto& [name, op2] : table)
        if (op == name) { *out = op2; return true; }
    return false;
}
} // namespace

void Compiler::adjust(int delta) {
    fn_->depth += delta;
    if (fn_->depth > fn_->maxDepth) fn_->maxDepth = fn_->depth;
}

void Compiler::emit(Op op, std::uint64_t operand, SourcePos pos, int stackEffect) {
    fn_->mod->emit(op, operand, line(pos));
    adjust(stackEffect);
}

std::size_t Compiler::emitJump(Op op, SourcePos pos, int stackEffect) {
    const std::size_t at = fn_->mod->emitJump(op, line(pos));
    adjust(stackEffect);
    return at;
}

Compiler::LocalInfo Compiler::declareLocal(const std::string& name, BindingKind kind, bool boxed) {
    LocalInfo info{newSlot(), kind, boxed, false};
    fn_->scopes.back()[name] = info;
    return info;
}

const Compiler::LocalInfo* Compiler::findInFunction(FunctionState* f, const std::string& name) {
    for (auto it = f->scopes.rbegin(); it != f->scopes.rend(); ++it) {
        auto hit = it->find(name);
        if (hit != it->end()) return &hit->second;
    }
    return nullptr;
}

// Makes `name` (bound in some function enclosing `f`) available in `f`, adding
// a capture in every function between the binding and `f`. The capture slot
// is recorded in `f`'s outermost scope so later references reuse it.
Compiler::LocalInfo Compiler::captureInto(FunctionState* f, const std::string& name,
                                          SourcePos pos, bool* found) {
    if (const LocalInfo* own = findInFunction(f, name)) { *found = true; return *own; }
    if (!f->parent) { *found = false; return {}; }
    LocalInfo outer = captureInto(f->parent, name, pos, found);
    if (!*found) return {};
    LocalInfo mine{f->nextSlot++, outer.kind, outer.boxed, /*captured=*/true};
    f->mod->addCapture(outer.slot, mine.slot);
    f->scopes.front()[name] = mine;
    return mine;
}

Compiler::Resolution Compiler::resolve(const std::string& name, SourcePos pos) {
    bool found = false;
    LocalInfo info = captureInto(fn_, name, pos, &found);
    if (found) return Resolution{RefKind::Local, info, info.kind};
    if (auto g = globals_.find(name)) return Resolution{RefKind::Global, {}, *g};
    throw CompileError("Not found: " + name, pos);
}
```

(The top-level code is an ordinary `FunctionState` without a parent; its block scopes
may bind locals that lambdas capture.)

Identifiers, loads and stores:

```cpp
void Compiler::loadLocal(const LocalInfo& info, SourcePos pos) {
    emit(info.boxed ? Op::PUSH_CELL : Op::PUSH_LOCAL, info.slot, pos, +1);
}

void Compiler::storeLocal(const LocalInfo& info, SourcePos pos) {
    emit(info.boxed ? Op::STORE_CELL : Op::STORE_LOCAL, info.slot, pos, -1);
}

void Compiler::compileIdent(const Ident& id) {
    const Resolution r = resolve(id.name, id.pos);
    if (r.ref == RefKind::Local) loadLocal(r.local, id.pos);
    else emit(Op::PUSH_GLOBAL, fn_->mod->addSymbol(id.name), id.pos, +1);
    if (r.kind == BindingKind::ParamlessDef) emit(Op::CALL, 0, id.pos, 0);
    else if (r.kind == BindingKind::LazyVal) emit(Op::FORCE, 0, id.pos, 0);
}
```

Calls, sends and operators:

```cpp
void Compiler::compileApply(const Apply& a) {
    for (const auto& arg : a.args) {
        if (arg->kind == NodeKind::NamedArg)
            throw CompileError("named arguments are not implemented yet", arg->pos);
    }
    if (a.fn->kind == NodeKind::Select) {
        const auto& sel = as<Select>(*a.fn);
        if (a.args.size() == 1 && (sel.name == "&&" || sel.name == "||")) {
            compileShortCircuit(*sel.qualifier, *a.args[0], sel.name == "&&", a.pos);
            return;
        }
        Op fast;
        if (a.args.size() == 1 && a.args[0]->kind != NodeKind::Splice &&
            isFastBinary(sel.name, &fast)) {
            compileExpr(*sel.qualifier);
            compileExpr(*a.args[0]);
            emit(fast, 0, a.pos, -1);
            return;
        }
        compileExpr(*sel.qualifier);
        for (const auto& arg : a.args) {
            if (arg->kind == NodeKind::Splice)
                throw CompileError("splices are only supported in function calls", arg->pos);
            compileExpr(*arg);
        }
        const auto n = static_cast<std::uint32_t>(a.args.size());
        emit(Op::SEND, fn_->mod->addSendSite(sel.name, n), a.pos, -static_cast<int>(n));
        return;
    }
    compileExpr(*a.fn);
    compileArgsAndCall(a.args, a.pos);
}

void Compiler::compileArgsAndCall(const std::vector<NodePtr>& args, SourcePos pos) {
    const bool spread = !args.empty() && args.back()->kind == NodeKind::Splice;
    for (std::size_t k = 0; k < args.size(); ++k) {
        const Node& arg = *args[k];
        if (arg.kind == NodeKind::Splice) {
            if (k + 1 != args.size())
                throw CompileError("a splice must be the last argument", arg.pos);
            compileExpr(*as<Splice>(arg).expr);
        } else {
            compileExpr(arg);
        }
    }
    const int n = static_cast<int>(args.size());
    if (spread) emit(Op::CALL_SPREAD, n - 1, pos, -n);
    else        emit(Op::CALL, n, pos, -n);
}

void Compiler::compileSelect(const Select& s) {
    compileExpr(*s.qualifier);
    if (s.name == "unary_-") { emit(Op::NEG, 0, s.pos, 0); return; }
    if (s.name == "unary_!") { emit(Op::NOT, 0, s.pos, 0); return; }
    emit(Op::SEND, fn_->mod->addSendSite(s.name, 0), s.pos, 0);
}

// a && b  ==>  a; JUMP_IF_FALSE Lf; b; JUMP Lend; Lf: PUSH_FALSE; Lend:
// a || b  ==>  a; JUMP_IF_TRUE  Lt; b; JUMP Lend; Lt: PUSH_TRUE;  Lend:
void Compiler::compileShortCircuit(const Node& lhs, const Node& rhs, bool isAnd, SourcePos pos) {
    compileExpr(lhs);
    const std::size_t toShort = emitJump(isAnd ? Op::JUMP_IF_FALSE : Op::JUMP_IF_TRUE, pos, -1);
    compileExpr(rhs);
    const std::size_t toEnd = emitJump(Op::JUMP, pos, 0);
    fn_->mod->patchJumpTo(toShort, fn_->mod->pos());
    adjust(-1);  // the rhs value is not on the stack on this path
    emit(isAnd ? Op::PUSH_FALSE : Op::PUSH_TRUE, 0, pos, +1);
    fn_->mod->patchJumpTo(toEnd, fn_->mod->pos());
}
```

Control flow (the depth bookkeeping at the join points is the part to get right):

```cpp
void Compiler::compileIf(const If& i) {
    compileExpr(*i.cond);
    const std::size_t toElse = emitJump(Op::JUMP_IF_FALSE, i.pos, -1);
    compileExpr(*i.thenp);                       // depth d+1
    const std::size_t toEnd = emitJump(Op::JUMP, i.pos, 0);
    fn_->mod->patchJumpTo(toElse, fn_->mod->pos());
    adjust(-1);                                  // else branch starts at depth d
    compileExpr(*i.elsep);                       // back to d+1
    fn_->mod->patchJumpTo(toEnd, fn_->mod->pos());
}

void Compiler::compileWhile(const While& w) {
    const std::size_t top = fn_->mod->pos();
    compileExpr(*w.cond);
    const std::size_t toEnd = emitJump(Op::JUMP_IF_FALSE, w.pos, -1);
    compileExpr(*w.body);
    emit(Op::POP, 0, w.pos, -1);
    fn_->mod->emitJumpBack(top, line(w.pos));
    fn_->mod->patchJumpTo(toEnd, fn_->mod->pos());
    emit(Op::PUSH_UNIT, 0, w.pos, +1);
}

void Compiler::compileReturn(const Return& r) {
    if (!fn_->allowsReturn)
        throw CompileError(fn_->isTopLevel ? "return outside a method definition"
                                           : "return inside a lambda is not supported",
                           r.pos);
    if (r.value) compileExpr(*r.value);
    else emit(Op::PUSH_UNIT, 0, r.pos, +1);
    emit(Op::RETURN, 0, r.pos, -1);
    adjust(+1);  // the expression `return e` has type Nothing; keep the stack shape
}
```

Blocks (hoisting and boxing):

```cpp
void Compiler::compileBlock(const Block& b) {
    fn_->scopes.emplace_back();
    // 1. Declare every definition of the block; boxed ones get a Cell now.
    for (const auto& s : b.stats) {
        if (s->kind == NodeKind::DefDef) {
            const auto& d = as<DefDef>(*s);
            const BindingKind k = d.paramLists.empty() ? BindingKind::ParamlessDef : BindingKind::Def;
            const LocalInfo info = declareLocal(d.name, k, boxed_.count(s.get()) > 0);
            if (info.boxed) emit(Op::MAKE_CELL, info.slot, d.pos, 0);
        } else if (s->kind == NodeKind::ValDef) {
            const auto& v = as<ValDef>(*s);
            const BindingKind k = v.isLazy ? BindingKind::LazyVal
                                : v.isVar  ? BindingKind::Var : BindingKind::Val;
            const LocalInfo info = declareLocal(v.name, k, boxed_.count(s.get()) > 0);
            if (info.boxed) emit(Op::MAKE_CELL, info.slot, v.pos, 0);
        }
    }
    // 2. Hoisted local defs.
    for (const auto& s : b.stats) {
        if (s->kind != NodeKind::DefDef) continue;
        const auto& d = as<DefDef>(*s);
        static const std::vector<Param> none;
        compileFunction(d.name, d.paramLists.empty() ? none : d.paramLists[0], *d.body,
                        /*isDef=*/true, d.pos);
        storeLocal(*findInFunction(fn_, d.name), d.pos);
    }
    // 3. Statements in order; the last expression is the block's value.
    bool valueOnStack = false;
    for (std::size_t k = 0; k < b.stats.size(); ++k) {
        const Node& s = *b.stats[k];
        const bool last = (k + 1 == b.stats.size());
        if (s.kind == NodeKind::DefDef || s.kind == NodeKind::Import) continue;
        if (s.kind == NodeKind::ValDef) {
            const auto& v = as<ValDef>(s);
            if (v.isLazy) compileLazyThunk(*v.rhs, v.pos);
            else compileExpr(*v.rhs);
            storeLocal(*findInFunction(fn_, v.name), v.pos);
            continue;
        }
        compileExpr(s);
        if (last) valueOnStack = true;
        else emit(Op::POP, 0, s.pos, -1);
    }
    if (!valueOnStack) emit(Op::PUSH_UNIT, 0, b.pos, +1);
    fn_->scopes.pop_back();
}
```

Functions and lambdas:

```cpp
void Compiler::compileFunction(const std::string& name, const std::vector<Param>& params,
                               const Node& body, bool isDef, SourcePos pos) {
    for (const Param& p : params) {
        if (p.defaultValue) throw CompileError("default parameter values are not implemented yet", p.pos);
        if (p.byName) throw CompileError("by-name parameters are not supported yet", p.pos);
    }
    auto mod = std::make_unique<BytecodeModule>();
    mod->setName(name);
    FunctionState fs;
    fs.mod = mod.get();
    fs.parent = fn_;
    fs.allowsReturn = isDef;
    fs.scopes.emplace_back();
    FunctionState* saved = fn_;
    fn_ = &fs;
    for (const Param& p : params) {
        const int slot = newSlot();
        if (p.name != "_") fs.scopes.back()[p.name] = LocalInfo{slot, BindingKind::Param, false, false};
    }
    mod->setArity(static_cast<int>(params.size()));
    mod->setVariadic(!params.empty() && params.back().repeated);
    analyseCaptures(params, body);
    compileExpr(body);
    emit(Op::RETURN, 0, pos, -1);
    mod->setLocalCount(fs.nextSlot - static_cast<int>(params.size()));
    mod->setMaxStack(fs.maxDepth);
    fn_ = saved;
    // In the enclosing function: push the captured slot values, then MAKE_FN.
    for (const auto& spec : mod->captureSpecs())
        emit(Op::PUSH_LOCAL, spec.parentSlot, pos, +1);  // a boxed slot pushes its Cell
    const int nCaps = mod->captureCount();
    const std::size_t index = fn_->mod->addBlock(std::move(mod));
    emit(Op::MAKE_FN, index, pos, 1 - nCaps);
}

void Compiler::compileLazyThunk(const Node& rhs, SourcePos pos) {
    compileFunction("<lazy>", {}, rhs, /*isDef=*/false, pos);
    emit(Op::MAKE_LAZY, 0, pos, 0);
}
```

`analyseCaptures(params, body)`: walk `body` with a stack of scopes mapping names to
`(const Node* decl, int functionDepth, kind)`, where parameters are declarations at depth 0
with `decl = nullptr`; a `Block` first declares all its `ValDef`/`DefDef` names, then walks
its statements; a `Lambda` or `DefDef` body is walked at `depth + 1` (a `DefDef`'s own name
is already declared in its block, so self-reference is seen as a deeper reference). On an
`Ident` (including an `Assign` target) that resolves to a declaration at a smaller depth,
insert `decl` into `boxed_` when the declaration is a `var`, a local `def`, or a `val`/`lazy
val` and the reference occurs inside a local `def` body (track "inside a def" on the walk
stack). Names not found in the walk (globals, builtins) are ignored. Parameters are never
boxed (immutable, and bound before any hoisted `MAKE_FN` runs). The right-hand side of a
`lazy val` is compiled as a thunk function, so it is walked at `depth + 1` like a lambda
body (a `var` assigned inside a lazy initialiser is therefore boxed).

`compileAssign` double-checks the analysis: assigning to a `Var` that was reached through a
capture but is not boxed is a compiler bug — throw
`std::logic_error("compiler: captured var is not boxed")` rather than silently writing a
copy.

`compileExpr` dispatch (every `NodeKind`; no `default:`):

- `IntLit` → `PUSH_CONST addInt(value)` or `addBigInt(digits, base)`; `FloatLit` →
  `addDouble`; `StringLit` → `addString`; `CharLit` → `addChar`; `BoolLit` →
  `PUSH_TRUE`/`PUSH_FALSE`; `NullLit` → `PUSH_NULL`; `UnitLit` → `PUSH_UNIT` (all +1).
- `InterpString` → `CompileError("string interpolation is not implemented yet")`;
  `Tuple` → "tuples are not implemented yet"; `Splice` → "a splice must be the last
  argument of a function call"; `NamedArg` → "named arguments are not implemented yet".
- `Ident`, `Select`, `Apply`, `Assign`, `If`, `While`, `Return`, `Block` → the functions
  above; `TypeApply` → `compileExpr(*fn)` (erased); `Lambda` →
  `compileFunction("<lambda>", params, *body, false, pos)`.
- `ValDef`/`DefDef`/`Import` outside a block (they only occur in blocks and at top level)
  → wrap in a one-statement `Block` path is not needed: `compileExpr` is never called on
  them; assert via `throw CompileError("definition used as an expression", pos)`.
- `Infix`, `Prefix`, `Parens`, `Typed` cannot occur after Desugar: throw
  `std::logic_error("compiler: node kind not desugared")`.

`compileAssign`: `Ident` target → `resolve`; `Var` locals → `compileExpr(value)`,
`storeLocal`; `Var` globals → `compileExpr(value)`, `STORE_GLOBAL addSymbol(name)` (−1);
then `PUSH_UNIT`. Any other kind → "Reassignment to val <name>".

`compileUnit(unit, mode, replResultIndex)`:

```cpp
CompiledUnit Compiler::compileUnit(const CompilationUnit& unit, UnitMode mode,
                                   int replResultIndex) {
    CompiledUnit out;
    out.module = std::make_unique<BytecodeModule>();
    out.module->setName("<top>");
    FunctionState top;
    top.mod = out.module.get();
    top.isTopLevel = true;
    top.scopes.emplace_back();
    fn_ = &top;
    boxed_.clear();
    // 1. Declare every top-level name; validate @main.
    const DefDef* main = nullptr;
    for (const auto& s : unit.stats) {
        if (s->kind == NodeKind::DefDef) {
            const auto& d = as<DefDef>(*s);
            globals_.declare(d.name, d.paramLists.empty() ? BindingKind::ParamlessDef
                                                          : BindingKind::Def);
            if (d.isMain()) {
                if (main) throw CompileError("only one @main method is allowed per file", d.pos);
                main = &d;
            }
            if (mode == UnitMode::Repl) out.definitions.push_back("def " + d.name);
        } else if (s->kind == NodeKind::ValDef) {
            const auto& v = as<ValDef>(*s);
            globals_.declare(v.name, v.isLazy ? BindingKind::LazyVal
                                     : v.isVar ? BindingKind::Var : BindingKind::Val);
            if (mode == UnitMode::Repl)
                out.definitions.push_back(std::string(v.isLazy ? "lazy val " : v.isVar ? "var " : "val ") + v.name);
        }
    }
    if (main) {
        const auto& lists = main->paramLists;
        const bool noParams = lists.empty() || (lists.size() == 1 && lists[0].empty());
        const bool varargs = lists.size() == 1 && lists[0].size() == 1 && lists[0][0].repeated;
        if (!noParams && !varargs)
            throw CompileError("@main methods take no parameters or a single repeated "
                               "String parameter", main->pos);
        out.mainName = main->name;
        out.mainTakesArgs = varargs;
    }
    // 2. Hoisted top-level defs.
    for (const auto& s : unit.stats) {
        if (s->kind != NodeKind::DefDef) continue;
        const auto& d = as<DefDef>(*s);
        static const std::vector<Param> none;
        compileFunction(d.name, d.paramLists.empty() ? none : d.paramLists[0], *d.body, true, d.pos);
        emit(Op::STORE_GLOBAL, top.mod->addSymbol(d.name), d.pos, -1);
    }
    // 3. Boxing analysis for code that runs in the top-level frame (top-level
    //    names themselves are globals and never boxed).
    for (const auto& s : unit.stats) {
        if (s->kind == NodeKind::ValDef) analyseCaptures({}, *as<ValDef>(*s).rhs);
        else if (s->kind != NodeKind::DefDef && s->kind != NodeKind::Import) analyseCaptures({}, *s);
    }
    // 4. Initialisers and statements in order.
    for (std::size_t k = 0; k < unit.stats.size(); ++k) {
        const Node& s = *unit.stats[k];
        const bool last = (k + 1 == unit.stats.size());
        if (s.kind == NodeKind::DefDef || s.kind == NodeKind::Import) continue;
        if (s.kind == NodeKind::ValDef) {
            const auto& v = as<ValDef>(s);
            if (v.isLazy) compileLazyThunk(*v.rhs, v.pos);
            else compileExpr(*v.rhs);
            emit(Op::STORE_GLOBAL, top.mod->addSymbol(v.name), v.pos, -1);
            continue;
        }
        compileExpr(s);
        if (mode == UnitMode::Repl && last) {
            out.resultName = "res" + std::to_string(replResultIndex);
            globals_.declare(out.resultName, BindingKind::Val);
            emit(Op::STORE_GLOBAL, top.mod->addSymbol(out.resultName), s.pos, -1);
        } else {
            emit(Op::POP, 0, s.pos, -1);
        }
    }
    emit(Op::PUSH_UNIT, 0, SourcePos{}, +1);
    emit(Op::RETURN, 0, SourcePos{}, -1);
    top.mod->setLocalCount(top.nextSlot);
    top.mod->setMaxStack(top.maxDepth);
    fn_ = nullptr;
    return out;
}
```

Top-level expression statements may contain blocks with locals and lambdas that capture
them: they use the top-level `FunctionState`'s slots (its `localCount`).

- [ ] **Step 5: Run to verify pass**

Run: `cmake --build build_release && ctest --test-dir build_release -R Compiler --output-on-failure`
Expected: all `Compiler.*` PASS.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/compiler tests/unit
git commit -m "compiler: AST to bytecode (scopes, boxed captures, fast paths, @main)

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```


---

### Task 8: Runtime layout, values, stack guard and the execution engine

**Files:**
- Create: `src/runtime/Errors.h`, `src/runtime/StackGuard.h`, `src/runtime/StackGuard.cpp`, `src/runtime/Runtime.h`, `src/runtime/Runtime.cpp`, `src/runtime/Values.h`, `src/runtime/Values.cpp`, `src/runtime/ExecutionEngine.h`, `src/runtime/ExecutionEngine.cpp`
- Create: `tests/unit/test_values.cpp`, `tests/unit/test_stackguard.cpp`, `tests/unit/test_engine.cpp`, `tests/unit/EvalHarness.h`
- Modify: `CMakeLists.txt` (add `protoscala_runtime`), `tests/unit/CMakeLists.txt`

**Interfaces:**
- Consumes: `BytecodeModule`, `Op`, `Compiler`, `GlobalTable`, `CompiledUnit` (Tasks 6–7); protoCore (`ProtoSpace`, `ProtoContext`, `isSmallInt`, `asSmallInt`, `smallIntInRange`, `makeSmallInt`, `ProtoObject::add/subtract/multiply/negate/partialCompare/getAttribute/getOwnAttributeDirect/hasOwnAttribute/hasAttribute/setAttribute/newChild/isMethod/asMethod/asMethodSelf`, `ProtoList`, `ProtoString`).
- Produces:
  - `class ScalaError : public std::runtime_error { ScalaError(std::string className, std::string message); const std::string& className() const; const std::string& message() const; int line = 0; }` — `what()` is `"<Class>: <message>"`.
  - `class StackOverflowError : public ScalaError`, `void checkNativeStack()`, `void configureThreadStacks()`, `int runOnEvaluatorThread(int (*)(void*), void*)`, `kThreadStackBytes = 32 MiB`.
  - `struct RuntimeLayout` and `class Runtime { explicit Runtime(proto::ProtoSpace&); proto::ProtoContext* rootContext() const; const RuntimeLayout& layout() const; }`.
  - `Values.h`: `isCharFast`, `charValueFast`, `isDoubleFast`, `isLargeIntFast`, `isIntegerFast`, `isNumberFast`, `isListFast`, `std::string formatDouble(double)`, `std::string show(ctx, layout, v)`, `bool valuesEqual(ctx, layout, a, b)`, `std::string typeName(ctx, layout, v)`, `const proto::ProtoObject* toScalaString(ctx, layout, v)`.
  - `class ExecutionEngine { explicit ExecutionEngine(const RuntimeLayout&); const proto::ProtoObject* run(proto::ProtoContext* parent, const BytecodeModule& mod); const proto::ProtoObject* invoke(proto::ProtoContext* ctx, const proto::ProtoObject* callable, const proto::ProtoObject* const* args, unsigned argc); const proto::ProtoObject* send(proto::ProtoContext* ctx, const proto::ProtoObject* receiver, const proto::ProtoString* name, const proto::ProtoObject* const* args, unsigned argc); }`.
  - `struct ActiveCallContext { ExecutionEngine* engine; const RuntimeLayout* layout; }`, `const ActiveCallContext* activeCallContext();` (thread-local, installed by `run` and restored on every exit path).

GC and rooting rules the engine follows (P1, P2):

- Each `execute` owns one `ProtoContext frame(parent->space, parent)` whose automatic locals
  are `[params | locals | operand stack]`, sized once to `arity + localCount + maxStack`
  (never resized, so a raw pointer to the slot array stays valid for the frame).
- Arguments are read from the caller's operand-stack slots (still rooted while the callee
  runs); a native call builds its argument `ProtoList` in a short-lived child context
  (protoClojure `src/runtime/ExecutionEngine.cpp:786-808`).
- Intermediate cells allocated through the frame's context are young cells of a live
  context and are not reclaimed before the context is destroyed or submits them at a
  safepoint (protoClojure `src/runtime/ListBuilder.h:12-22`); every result is stored into a
  slot before the next instruction.
- A value returned from a frame is anchored through `frame.returnValue` before the frame is
  destroyed (`~ProtoContext` hands it to `previous`, as protoClojure
  `src/runtime/ListBuilder.cpp:465-474` relies on).
- `JUMP_BACK` calls `frame.safepoint()` (Open question Q21): at a back edge every live value
  of the frame is in a slot, so the STW handshake and young-generation submission are safe;
  without it an allocating loop keeps all of its garbage until the frame returns (R1).

- [ ] **Step 1: Write the failing tests**

`tests/unit/test_values.cpp`:

```cpp
#include "runtime/Runtime.h"
#include "runtime/Values.h"
#include "protoCore.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

using namespace protoScala;

TEST(Values, FormatDoubleMatchesJava) {
    EXPECT_EQ(formatDouble(1.0), "1.0");
    EXPECT_EQ(formatDouble(100.0), "100.0");
    EXPECT_EQ(formatDouble(0.1 + 0.2), "0.30000000000000004");
    EXPECT_EQ(formatDouble(1.0 / 3), "0.3333333333333333");
    EXPECT_EQ(formatDouble(1e7), "1.0E7");
    EXPECT_EQ(formatDouble(1e21), "1.0E21");
    EXPECT_EQ(formatDouble(1e-5), "1.0E-5");
    EXPECT_EQ(formatDouble(0.001), "0.001");
    EXPECT_EQ(formatDouble(-0.0), "-0.0");
    EXPECT_EQ(formatDouble(std::numeric_limits<double>::infinity()), "Infinity");
    EXPECT_EQ(formatDouble(-std::numeric_limits<double>::infinity()), "-Infinity");
    EXPECT_EQ(formatDouble(std::nan("")), "NaN");
}

TEST(Values, ShowAndEquality) {
    proto::ProtoSpace space;
    Runtime rt(space);
    proto::ProtoContext ctx(&space, rt.rootContext());
    const RuntimeLayout& L = rt.layout();
    EXPECT_EQ(show(&ctx, L, ctx.fromInteger(42)), "42");
    EXPECT_EQ(show(&ctx, L, ctx.fromString("123456789012345678901234567890", 10)),
              "123456789012345678901234567890");
    EXPECT_EQ(show(&ctx, L, ctx.fromDouble(2.5)), "2.5");
    EXPECT_EQ(show(&ctx, L, PROTO_TRUE), "true");
    EXPECT_EQ(show(&ctx, L, PROTO_NONE), "null");
    EXPECT_EQ(show(&ctx, L, L.unit), "()");
    EXPECT_EQ(show(&ctx, L, ctx.fromUnicodeChar(U'x')), "x");
    EXPECT_EQ(show(&ctx, L, ctx.fromUTF8String("hi")), "hi");
    const proto::ProtoObject* items[] = {ctx.fromInteger(1), ctx.fromInteger(2)};
    EXPECT_EQ(show(&ctx, L, ctx.newList(2, items)->asObject(&ctx)), "List(1, 2)");

    EXPECT_TRUE(valuesEqual(&ctx, L, ctx.fromInteger(1), ctx.fromDouble(1.0)));
    EXPECT_TRUE(valuesEqual(&ctx, L, ctx.fromUTF8String("ab"), ctx.fromUTF8String("ab")));
    EXPECT_TRUE(valuesEqual(&ctx, L, ctx.fromUnicodeChar(U'a'), ctx.fromInteger(97)));
    EXPECT_TRUE(valuesEqual(&ctx, L, PROTO_NONE, PROTO_NONE));
    const proto::ProtoObject* nan = ctx.fromDouble(std::nan(""));
    EXPECT_FALSE(valuesEqual(&ctx, L, nan, nan));
    EXPECT_FALSE(valuesEqual(&ctx, L, ctx.fromUTF8String("1"), ctx.fromInteger(1)));

    EXPECT_EQ(typeName(&ctx, L, ctx.fromInteger(1)), "Int");
    EXPECT_EQ(typeName(&ctx, L, ctx.fromDouble(1)), "Double");
    EXPECT_EQ(typeName(&ctx, L, ctx.fromUTF8String("s")), "String");
    EXPECT_EQ(typeName(&ctx, L, PROTO_NONE), "Null");
}

TEST(Values, PrimitivePrototypesAreRebound) {
    proto::ProtoSpace space;
    Runtime rt(space);
    proto::ProtoContext ctx(&space, rt.rootContext());
    EXPECT_EQ(ctx.fromInteger(1)->getPrototype(&ctx), rt.layout().intProto);
    EXPECT_EQ(ctx.fromUTF8String("abcdefghij")->getPrototype(&ctx), rt.layout().stringProto);
    EXPECT_EQ(ctx.fromDouble(1.5)->getPrototype(&ctx), rt.layout().doubleProto);
    EXPECT_EQ(PROTO_TRUE->getPrototype(&ctx), rt.layout().booleanProto);
}
```

`tests/unit/test_stackguard.cpp`:

```cpp
#include "runtime/StackGuard.h"

#include <gtest/gtest.h>

using namespace protoScala;

namespace {
int recurse(int n) {
    checkNativeStack();
    volatile char pad[256];
    pad[0] = static_cast<char>(n);
    return recurse(n + 1) + pad[0];
}
}

TEST(StackGuard, ShallowCallsDoNotThrow) { EXPECT_NO_THROW(checkNativeStack()); }

TEST(StackGuard, EvaluatorThreadReturnsTheBodyResult) {
    EXPECT_EQ(runOnEvaluatorThread([](void*) { return 7; }, nullptr), 7);
}

TEST(StackGuard, UnboundedRecursionRaisesStackOverflowError) {
    const int rc = runOnEvaluatorThread([](void*) {
        try { recurse(0); } catch (const StackOverflowError& e) {
            return e.className() == "StackOverflowError" ? 0 : 2;
        }
        return 1;
    }, nullptr);
    EXPECT_EQ(rc, 0);
}

TEST(StackGuard, ExceptionsPropagateToTheCaller) {
    EXPECT_THROW(runOnEvaluatorThread([](void*) -> int { throw ScalaError("RuntimeException", "x"); },
                                      nullptr),
                 ScalaError);
}
```

`tests/unit/EvalHarness.h` (used by this task and Task 9):

```cpp
// Compiles and runs Scala snippets in one session for unit tests. Each
// eval() is one REPL-style unit: the value of its last expression is shown
// with Scala's toString, or "error: <Class>: <message>" is returned.
#pragma once
#include "compiler/Compiler.h"
#include "frontend/Desugar.h"
#include "frontend/Parser.h"
#include "runtime/Errors.h"
#include "runtime/ExecutionEngine.h"
#include "runtime/Runtime.h"
#include "runtime/Values.h"
#include "protoCore.h"

#include <memory>
#include <string>
#include <vector>

namespace protoScala::test {

class EvalHarness {
public:
    EvalHarness() : runtime_(space_), engine_(runtime_.layout()) {}

    Runtime& runtime() { return runtime_; }
    GlobalTable& globals() { return globals_; }

    std::string eval(const std::string& src) {
        auto unit = parseSource(src);
        desugar(*unit);
        Compiler compiler(globals_);
        CompiledUnit cu = compiler.compileUnit(*unit, UnitMode::Repl, counter_++);
        proto::ProtoContext ctx(&space_, runtime_.rootContext());
        cu.module->linkSymbols(&ctx);
        const BytecodeModule& mod = *cu.module;
        modules_.push_back(std::move(cu.module));
        try {
            engine_.run(&ctx, mod);
        } catch (const ScalaError& e) {
            return std::string("error: ") + e.what();
        }
        if (cu.resultName.empty()) return "";
        const auto* key = proto::ProtoString::createSymbol(&ctx, cu.resultName.c_str());
        const proto::ProtoObject* v = runtime_.layout().globals->getOwnAttributeDirect(&ctx, key);
        return show(&ctx, runtime_.layout(), v ? v : PROTO_NONE);
    }

private:
    proto::ProtoSpace space_;  // first member: destroyed last
    Runtime runtime_;
    ExecutionEngine engine_;
    GlobalTable globals_;
    std::vector<std::unique_ptr<BytecodeModule>> modules_;  // functions point into them
    int counter_ = 0;
};

} // namespace protoScala::test
```

`tests/unit/test_engine.cpp`:

```cpp
#include "EvalHarness.h"
#include "runtime/StackGuard.h"

#include <gtest/gtest.h>

using protoScala::test::EvalHarness;

TEST(Engine, ArithmeticAndPromotion) {
    EvalHarness h;
    EXPECT_EQ(h.eval("1 + 2 * 3"), "7");
    EXPECT_EQ(h.eval("9007199254740991L + 1"), "9007199254740992");
    EXPECT_EQ(h.eval("-9007199254740992L - 1"), "-9007199254740993");
    EXPECT_EQ(h.eval("100000000000L * 100000000000L"), "10000000000000000000000");
    EXPECT_EQ(h.eval("-(-9007199254740992L)"), "9007199254740992");
    EXPECT_EQ(h.eval("1.5 + 1"), "2.5");
}

TEST(Engine, ComparisonsAndEquality) {
    EvalHarness h;
    EXPECT_EQ(h.eval("1 < 2"), "true");
    EXPECT_EQ(h.eval("2 <= 1"), "false");
    EXPECT_EQ(h.eval("1 == 1.0"), "true");
    EXPECT_EQ(h.eval("\"ab\" == \"a\" + \"b\""), "true");
    EXPECT_EQ(h.eval("1 != 2"), "true");
    EXPECT_EQ(h.eval("\"abc\" < \"abd\""), "true");
    EXPECT_EQ(h.eval("!true"), "false");
}

TEST(Engine, StringAndCharArithmetic) {
    EvalHarness h;
    EXPECT_EQ(h.eval("\"a\" + 1 + 2"), "a12");
    EXPECT_EQ(h.eval("1 + 2 + \"a\""), "3a");
    EXPECT_EQ(h.eval("'a' + 1"), "98");
    EXPECT_EQ(h.eval("\"x\" + true + null"), "xtruenull");
}

TEST(Engine, ControlFlow) {
    EvalHarness h;
    EXPECT_EQ(h.eval("if 1 < 2 then \"yes\" else \"no\""), "yes");
    EXPECT_EQ(h.eval("if false then 1"), "()");
    EXPECT_EQ(h.eval("{ var i = 0; var s = 0; while i < 10 do { s += i; i += 1 }; s }"), "45");
    EXPECT_EQ(h.eval("true && false || true"), "true");
}

TEST(Engine, FunctionsClosuresAndRecursion) {
    EvalHarness h;
    EXPECT_EQ(h.eval("{ def make(n: Int) = (x: Int) => x + n; make(10)(5) }"), "15");
    EXPECT_EQ(h.eval("{ var c = 0; val inc = () => { c += 1; c }; inc(); inc() }"), "2");
    EXPECT_EQ(h.eval("{ def fib(n: Int): Int = if n < 2 then n else fib(n - 1) + fib(n - 2); fib(20) }"),
              "6765");
    EXPECT_EQ(h.eval("{ def even(n: Int): Boolean = if n == 0 then true else odd(n - 1)\n"
                     "  def odd(n: Int): Boolean = if n == 0 then false else even(n - 1)\n"
                     "  even(100) }"),
              "true");
    EXPECT_EQ(h.eval("def add(a: Int)(b: Int) = a + b"), "");
    EXPECT_EQ(h.eval("add(3)(4)"), "7");
}

TEST(Engine, GlobalsPersistAcrossUnits) {
    EvalHarness h;
    EXPECT_EQ(h.eval("var total = 1"), "");
    EXPECT_EQ(h.eval("total = total + 41"), "()");
    EXPECT_EQ(h.eval("total"), "42");
    EXPECT_EQ(h.eval("res1"), "()");
}

TEST(Engine, LazyValsAndParamlessDefs) {
    EvalHarness h;
    EXPECT_EQ(h.eval("{ var n = 0; lazy val x = { n += 1; 42 }; x + x + n }"), "85");
    EXPECT_EQ(h.eval("{ var n = 0; def tick = { n += 1; n }; tick; tick; tick }"), "3");
}

TEST(Engine, VarargsAndSpread) {
    EvalHarness h;
    EXPECT_EQ(h.eval("def all(xs: Int*) = xs"), "");
    EXPECT_EQ(h.eval("all(1, 2, 3)"), "List(1, 2, 3)");
    EXPECT_EQ(h.eval("all()"), "List()");
    EXPECT_EQ(h.eval("{ def fwd(ys: Int*) = all(ys*); fwd(4, 5) }"), "List(4, 5)");
}

TEST(Engine, RuntimeErrors) {
    EvalHarness h;
    EXPECT_EQ(h.eval("if 1 then 2 else 3").rfind("error: ClassCastException", 0), 0u);
    EXPECT_EQ(h.eval("{ val f: Int => Int = null; f(1) }").rfind("error: NullPointerException", 0), 0u);
    EXPECT_EQ(h.eval("1.noSuchMethod").rfind("error: NoSuchMethodError", 0), 0u);
    EXPECT_EQ(h.eval("{ val f = (x: Int) => x; f(1, 2) }")
                  .rfind("error: IllegalArgumentException: wrong number of arguments", 0), 0u);
}

TEST(Engine, StackOverflowIsAnErrorAndTheSessionSurvives) {
    const int rc = protoScala::runOnEvaluatorThread([](void*) {
        EvalHarness h;
        const std::string r = h.eval("{ def f(n: Int): Int = f(n + 1) + 1; f(0) }");
        if (r.rfind("error: StackOverflowError", 0) != 0) return 1;
        return h.eval("1 + 1") == "2" ? 0 : 2;
    }, nullptr);
    EXPECT_EQ(rc, 0);
}

TEST(Engine, DeepButBoundedRecursionWorks) {
    const int rc = protoScala::runOnEvaluatorThread([](void*) {
        EvalHarness h;
        return h.eval("{ def sum(n: Int): Int = if n == 0 then 0 else n + sum(n - 1); sum(10000) }") ==
                       "50005000" ? 0 : 1;
    }, nullptr);
    EXPECT_EQ(rc, 0);
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build_release`
Expected: FAIL — `runtime/Runtime.h: No such file or directory`.

- [ ] **Step 3: Write `Errors.h` and the stack guard**

```cpp
// Errors.h — Scala-level runtime errors raised by the VM and the primitives.
#pragma once
#include <stdexcept>
#include <string>

namespace protoScala {

// A Scala exception: `className` is the unqualified Scala/Java class name
// (ArithmeticException, NullPointerException, ...; D8: no java.lang prefix),
// `message` its message. `line` is the innermost source line, filled by the
// VM frame the error passes through first (0: unknown).
class ScalaError : public std::runtime_error {
public:
    ScalaError(std::string className, std::string message)
        : std::runtime_error(className + ": " + message),
          className_(std::move(className)), message_(std::move(message)) {}
    const std::string& className() const { return className_; }
    const std::string& message() const { return message_; }
    int line = 0;
private:
    std::string className_;
    std::string message_;
};

} // namespace protoScala
```

`StackGuard.h` / `StackGuard.cpp`: copy protoClojure `src/runtime/StackGuard.h:1-103` and
`src/runtime/StackGuard.cpp:1-153` (namespace `protoScala`), with these changes only:

- `StackOverflowError` derives from `ScalaError`:
  ```cpp
  class StackOverflowError : public ScalaError {
  public:
      explicit StackOverflowError(const std::string& message)
          : ScalaError("StackOverflowError", message) {}
  };
  ```
- Keep `StackUse` (the parser and compiler use `StackUse::Source` when they add recursion
  checks); the evaluation message becomes
  `"calls nested too deeply for the " + describeBytes(tl_stackBytes) + " thread stack (use a while loop for deep iteration)"`
  and the source message `"source nested too deeply for the ... thread stack"`.
- Header comment: replace Clojure references (loop/recur, futures, pmap) by "the script
  runner and the REPL run on the evaluator thread; later phases' worker threads get the
  same stack size through configureThreadStacks".

- [ ] **Step 4: Write `Runtime.h` / `Runtime.cpp`**

```cpp
/*
 * Runtime — the protoCore objects one protoScala session needs, built once
 * per ProtoSpace and pinned in the root context's automatic-local slots for
 * the whole session (P1). Drivers never use root-context slots themselves;
 * they create child contexts.
 *
 * Primitive prototypes: protoCore aliases the integer, double, none and
 * method prototypes to objectPrototype and creates the string, boolean, char
 * and list prototypes immutable (core/ProtoSpace.cpp:1185-1216). Like
 * protoST (src/runtime/Bootstrap.cpp:88-103), the Runtime creates mutable
 * protoScala prototypes and rebinds the ProtoSpace fields to them, so a raw
 * SmallInteger, ProtoString or ProtoList answers Scala methods (DESIGN §4.1,
 * §6). One runtime per process (R5).
 */
#pragma once

namespace proto {
class ProtoContext;
class ProtoObject;
class ProtoSpace;
class ProtoString;
}

namespace protoScala {

inline constexpr unsigned kMaxFunctionArity = 22;

struct RuntimeLayout {
    // Every pointer below is pinned in a root-context slot (Runtime.cpp).
    proto::ProtoObject* globals = nullptr;        // mutable: module and session globals
    proto::ProtoObject* anyProto = nullptr;       // Any: parent of every Scala prototype
    proto::ProtoObject* intProto = nullptr;       // Int = Long = BigInt (D1)
    proto::ProtoObject* doubleProto = nullptr;    // Double = Float (D2)
    proto::ProtoObject* booleanProto = nullptr;
    proto::ProtoObject* charProto = nullptr;
    proto::ProtoObject* stringProto = nullptr;
    proto::ProtoObject* listProto = nullptr;      // List (varargs arrive as List)
    proto::ProtoObject* unitProto = nullptr;
    proto::ProtoObject* functionProto = nullptr;  // parent of Function0..22, FunctionXXL
    proto::ProtoObject* cellProto = nullptr;      // boxed captured variables
    proto::ProtoObject* lazyProto = nullptr;      // lazy val holders
    proto::ProtoObject* functionArity[kMaxFunctionArity + 2] = {};  // [23] = FunctionXXL
    const proto::ProtoObject* unit = nullptr;     // the () singleton (DESIGN §4.1)
    // Interned attribute keys (strong symbols of this space).
    const proto::ProtoString* codeKey = nullptr;      // "__code__": BytecodeModule address
    const proto::ProtoString* capturesKey = nullptr;  // "__captures__": ProtoList
    const proto::ProtoString* valueKey = nullptr;     // "__value__": Cell / Lazy value
    const proto::ProtoString* thunkKey = nullptr;     // "__thunk__": Lazy initialiser
    const proto::ProtoString* applyName = nullptr;    // "apply"

    const proto::ProtoObject* functionProtoFor(unsigned arity) const {
        return functionArity[arity <= kMaxFunctionArity ? arity : kMaxFunctionArity + 1];
    }
};

class Runtime {
public:
    explicit Runtime(proto::ProtoSpace& space);
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    proto::ProtoContext* rootContext() const;
    const RuntimeLayout& layout() const { return layout_; }

private:
    proto::ProtoSpace& space_;
    RuntimeLayout layout_;
};

} // namespace protoScala
```

`Runtime.cpp`:

```cpp
#include "runtime/Runtime.h"
#include "protoCore.h"

namespace protoScala {

namespace {
enum RootSlot : unsigned {
    kGlobals, kAny, kInt, kDouble, kBoolean, kChar, kString, kList, kUnitProto,
    kFunction, kFunctionArities, kCell, kLazy, kUnit, kRootSlotCount
};
} // namespace

Runtime::Runtime(proto::ProtoSpace& space) : space_(space) {
    proto::ProtoContext* ctx = space.rootContext;
    ctx->resizeAutomaticLocals(kRootSlotCount);
    auto pin = [&](unsigned slot, const proto::ProtoObject* v) {
        ctx->setAutomaticLocal(slot, v);
        return const_cast<proto::ProtoObject*>(v);
    };
    RuntimeLayout& L = layout_;
    L.globals  = pin(kGlobals, space.objectPrototype->newChild(ctx, /*isMutable=*/true));
    L.anyProto = pin(kAny, space.objectPrototype->newChild(ctx, true));
    auto child = [&](unsigned slot) { return pin(slot, L.anyProto->newChild(ctx, true)); };
    L.intProto      = child(kInt);
    L.doubleProto   = child(kDouble);
    L.booleanProto  = child(kBoolean);
    L.charProto     = child(kChar);
    L.stringProto   = child(kString);
    L.listProto     = child(kList);
    L.unitProto     = child(kUnitProto);
    L.functionProto = child(kFunction);
    L.cellProto     = child(kCell);
    L.lazyProto     = child(kLazy);
    L.unit = pin(kUnit, L.unitProto->newChild(ctx));
    // Function0..Function22 and FunctionXXL, pinned together in one list.
    const proto::ProtoObject* arities[kMaxFunctionArity + 2];
    for (unsigned n = 0; n < kMaxFunctionArity + 2; ++n)
        arities[n] = L.functionProto->newChild(ctx, true);
    const proto::ProtoObject* list = ctx->newList(kMaxFunctionArity + 2, arities)->asObject(ctx);
    ctx->setAutomaticLocal(kFunctionArities, list);
    for (unsigned n = 0; n < kMaxFunctionArity + 2; ++n)
        L.functionArity[n] = const_cast<proto::ProtoObject*>(arities[n]);

    L.codeKey     = proto::ProtoString::createSymbol(ctx, "__code__");
    L.capturesKey = proto::ProtoString::createSymbol(ctx, "__captures__");
    L.valueKey    = proto::ProtoString::createSymbol(ctx, "__value__");
    L.thunkKey    = proto::ProtoString::createSymbol(ctx, "__thunk__");
    L.applyName   = proto::ProtoString::createSymbol(ctx, "apply");

    // Rebind the primitive prototypes (see the header comment).
    space.smallIntegerPrototype = L.intProto;
    space.largeIntegerPrototype = L.intProto;
    space.doublePrototype       = L.doubleProto;
    space.floatPrototype        = L.doubleProto;
    space.booleanPrototype      = L.booleanProto;
    space.unicodeCharPrototype  = L.charProto;
    space.stringPrototype       = L.stringProto;
    space.listPrototype         = L.listProto;
    space.methodPrototype       = L.functionProto;  // native functions are Scala functions
}

proto::ProtoContext* Runtime::rootContext() const { return space_.rootContext; }

} // namespace protoScala
```

(`arities` is a C array of pointers between allocations inside the constructor: the
objects are young cells of the root context, which lives for the whole space, so they
cannot be reclaimed before they are pinned — the same argument as protoClojure's
`main.cpp:99-178` marker set-up.)

- [ ] **Step 5: Write `Values.h` / `Values.cpp`**

```cpp
/*
 * Values — Scala semantics of protoCore values: tag tests, toString (show),
 * == (valuesEqual) and type names for error messages.
 */
#pragma once
#include "runtime/Runtime.h"
#include "protoCore.h"

#include <string>

namespace protoScala {

// Char: tag 1 + embedded type 2 in the low 10 bits, code point in bits 10..
// (protoCore headers/proto_internal.h:142,251, core/ProtoContext.cpp:788-794;
// protoCore has no public isUnicodeChar).
inline bool isCharFast(const proto::ProtoObject* v) {
    return (reinterpret_cast<unsigned long>(v) & 0x3FFUL) == 0x081UL;
}
inline char32_t charValueFast(const proto::ProtoObject* v) {
    return static_cast<char32_t>(reinterpret_cast<unsigned long>(v) >> 10);
}
// POINTER_TAG_DOUBLE = 15, POINTER_TAG_LARGE_INTEGER = 14 (headers/proto_internal.h:236-237).
inline bool isDoubleFast(const proto::ProtoObject* v) {
    return v && (reinterpret_cast<unsigned long>(v) & 0x3FUL) == 15;
}
inline bool isLargeIntFast(const proto::ProtoObject* v) {
    return v && (reinterpret_cast<unsigned long>(v) & 0x3FUL) == 14;
}
inline bool isIntegerFast(const proto::ProtoObject* v) {
    return proto::isSmallInt(v) || isLargeIntFast(v);
}
inline bool isNumberFast(const proto::ProtoObject* v) {
    return isIntegerFast(v) || isDoubleFast(v);
}
// POINTER_TAG_LIST = 2, POINTER_TAG_LIST_SMALL = 25 (protoClojure src/compiler/Compiler.cpp:16-36).
inline bool isListFast(const proto::ProtoObject* v) {
    const unsigned long t = reinterpret_cast<unsigned long>(v) & 0x3FUL;
    return v && (t == 2 || t == 25);
}

// Java Double.toString: shortest round-trip digits; plain notation for
// magnitudes in [1e-3, 1e7), otherwise d.dddE<exp>; "-0.0", "NaN",
// "Infinity", "-Infinity".
std::string formatDouble(double d);

// Scala toString of `v` (null, (), Int, Double, Boolean, Char, String,
// List(...), <functionN>, <object>).
std::string show(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* v);

// Scala `==`: cooperative numeric equality across Int/Double/Char (NaN is
// equal to nothing), strings by content, lists element-wise, identity
// otherwise. Allocates nothing.
bool valuesEqual(proto::ProtoContext* ctx, const RuntimeLayout& L,
                 const proto::ProtoObject* a, const proto::ProtoObject* b);

// "Int", "Double", "Boolean", "Char", "String", "Unit", "Null", "List",
// "Function", "Object".
std::string typeName(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* v);

// `v` as a ProtoString object: `v` itself when it is a string, else a new
// string holding show(v). Used by string concatenation; keeps ropes intact.
const proto::ProtoObject* toScalaString(proto::ProtoContext* ctx, const RuntimeLayout& L,
                                        const proto::ProtoObject* v);

} // namespace protoScala
```

`Values.cpp`:

- `formatDouble`: copy protoClojure `src/runtime/Primitives.cpp:2701-` (`formatDouble`, the
  Java `Double.toString` algorithm) and change only the special values: NaN → `"NaN"`,
  +∞ → `"Infinity"`, −∞ → `"-Infinity"`.
- `show`: `PROTO_NONE` → `"null"`; `v == L.unit` → `"()"`; `PROTO_TRUE`/`PROTO_FALSE`;
  SmallInt → `std::to_string(asSmallInt(v))`; LargeInteger →
  `v->asIntegerString(ctx, 10)->toStdString(ctx)` (as protoClojure
  `src/compiler/Compiler.cpp:69`); double → `formatDouble(v->asDouble(ctx))`; char → UTF-8
  of `charValueFast`; `ProtoObject::isStringTagFast(v)` →
  `reinterpret_cast<const proto::ProtoString*>(v)->toStdString(ctx)`; `isListFast` →
  `"List(" + show(elements) joined ", " + ")"` (call `checkNativeStack()` first: nested lists
  recurse); a compiled function (own `__code__` SmallInt attribute) →
  `"<function" + arity + ">"` (read the `BytecodeModule` arity); `v->isMethod(ctx)` →
  `"<function>"`; anything else → `"<object>"`.
- `valuesEqual`: if both are numbers or chars → convert a char to `fromInteger(code point)`
  semantics without allocating (compare `charValueFast` as an integer: when both are
  SmallInt/char compare the integers directly; otherwise `a->partialCompare(ctx, b) ==
  std::partial_ordering::equivalent`, which is false for NaN); `a == b` → true; both
  `isStringTagFast` → `partialCompare == equivalent`; both `isListFast` → equal sizes and
  pairwise `valuesEqual`; else false.
- `typeName`: by the tag tests above; `L.unit` → `"Unit"`; compiled function or method →
  `"Function"`; else `"Object"`.
- `toScalaString`: `isStringTagFast(v)` → `v`; else `ctx->fromUTF8String(show(ctx, L, v).c_str())`.

- [ ] **Step 6: Write `ExecutionEngine.h`**

```cpp
/*
 * ExecutionEngine — the recursive bytecode VM (DESIGN §3.6).
 *
 * execute() recurses natively once per call; each frame owns one
 * ProtoContext (P2) whose automatic locals hold parameters, locals and the
 * operand stack (P1), sized exactly from the module's metadata. The stack
 * guard runs on every call. Native primitives re-enter the VM through
 * activeCallContext()->engine->invoke(), which run() installs on the thread
 * and restores on every exit path (protoClojure src/runtime/Primitives.h:109-170).
 */
#pragma once
#include "runtime/Runtime.h"

namespace proto {
class ProtoContext;
class ProtoObject;
class ProtoString;
}

namespace protoScala {

class BytecodeModule;
class ExecutionEngine;

struct ActiveCallContext {
    ExecutionEngine* engine;
    const RuntimeLayout* layout;
};
const ActiveCallContext* activeCallContext();

class ExecutionEngine {
public:
    explicit ExecutionEngine(const RuntimeLayout& layout) : layout_(layout) {}

    // Top-level entry: runs an arity-0 module under a child frame of `parent`.
    // The result is anchored in `parent` (returnValue) but the caller must
    // still root it before allocating. Throws ScalaError.
    const proto::ProtoObject* run(proto::ProtoContext* parent, const BytecodeModule& mod);

    // Calls any Scala callable: compiled function, native method, or any
    // value with an `apply` method (DESIGN §5.1). `args` must be rooted.
    const proto::ProtoObject* invoke(proto::ProtoContext* ctx, const proto::ProtoObject* callable,
                                     const proto::ProtoObject* const* args, unsigned argc);

    // Scala method call `receiver.name(args)`; `args` must be rooted.
    const proto::ProtoObject* send(proto::ProtoContext* ctx, const proto::ProtoObject* receiver,
                                   const proto::ProtoString* name,
                                   const proto::ProtoObject* const* args, unsigned argc);

private:
    const RuntimeLayout& layout_;

    const proto::ProtoObject* execute(proto::ProtoContext* parent, const BytecodeModule& mod,
                                      const proto::ProtoObject* const* args, unsigned argc,
                                      const proto::ProtoObject* captures);
    const BytecodeModule* compiledModule(proto::ProtoContext* ctx, const proto::ProtoObject* v) const;
    const proto::ProtoObject* callNative(proto::ProtoContext* ctx, proto::ProtoMethod fn,
                                         const proto::ProtoObject* self,
                                         const proto::ProtoObject* const* args, unsigned argc);
    const proto::ProtoObject* force(proto::ProtoContext* ctx, const proto::ProtoObject* v);
    [[gnu::noinline]] const proto::ProtoObject* slowBinary(proto::ProtoContext* ctx, Op op,
                                                           const proto::ProtoObject* a,
                                                           const proto::ProtoObject* b);
};

} // namespace protoScala
```

(`ExecutionEngine.h` includes `compiler/Opcodes.h` for `Op` and `protoCore.h` for
`proto::ProtoMethod`; keep those two includes.)

- [ ] **Step 7: Write `ExecutionEngine.cpp` — dispatch loop and helpers**

```cpp
#include "runtime/ExecutionEngine.h"
#include "compiler/BytecodeModule.h"
#include "runtime/Errors.h"
#include "runtime/StackGuard.h"
#include "runtime/Values.h"
#include "protoCore.h"

#include <cmath>
#include <string>

namespace protoScala {

namespace {

thread_local ActiveCallContext tl_active{};
thread_local bool tl_activeSet = false;

struct ActiveGuard {
    ActiveCallContext saved;
    bool wasSet;
    ActiveGuard(ExecutionEngine* e, const RuntimeLayout* l) : saved(tl_active), wasSet(tl_activeSet) {
        tl_active = ActiveCallContext{e, l};
        tl_activeSet = true;
    }
    ~ActiveGuard() { tl_active = saved; tl_activeSet = wasSet; }
};

const char* opSymbol(Op op) {
    switch (op) {
        case Op::ADD: return "+";  case Op::SUB: return "-";  case Op::MUL: return "*";
        case Op::LT:  return "<";  case Op::LE:  return "<="; case Op::GT:  return ">";
        case Op::GE:  return ">="; default:      return "?";
    }
}

[[noreturn, gnu::cold]] void throwNotBoolean(proto::ProtoContext* ctx, const RuntimeLayout& L,
                                             const proto::ProtoObject* v) {
    throw ScalaError("ClassCastException", typeName(ctx, L, v) + " cannot be cast to Boolean");
}

} // namespace

const ActiveCallContext* activeCallContext() { return tl_activeSet ? &tl_active : nullptr; }

const proto::ProtoObject* ExecutionEngine::run(proto::ProtoContext* parent, const BytecodeModule& mod) {
    ActiveGuard guard(this, &layout_);
    return execute(parent, mod, nullptr, 0, nullptr);
}

const BytecodeModule* ExecutionEngine::compiledModule(proto::ProtoContext* ctx,
                                                      const proto::ProtoObject* v) const {
    // getOwnAttributeDirect returns nullptr for non-object receivers
    // (protoCore core/ProtoObject.cpp:1446-1449) and for a missing attribute.
    const proto::ProtoObject* code = v->getOwnAttributeDirect(ctx, layout_.codeKey);
    if (!code || !proto::isSmallInt(code)) return nullptr;
    return reinterpret_cast<const BytecodeModule*>(proto::asSmallInt(code));
}

const proto::ProtoObject* ExecutionEngine::callNative(proto::ProtoContext* ctx, proto::ProtoMethod fn,
                                                      const proto::ProtoObject* self,
                                                      const proto::ProtoObject* const* args,
                                                      unsigned argc) {
    proto::ProtoContext scope(ctx->space, ctx);
    const proto::ProtoList* list = scope.newList(argc, args);
    const proto::ProtoObject* r = fn(&scope, self, nullptr, list, nullptr);
    if (!r) r = PROTO_NONE;
    scope.returnValue = r;
    return r;
}

const proto::ProtoObject* ExecutionEngine::invoke(proto::ProtoContext* ctx,
                                                  const proto::ProtoObject* callee,
                                                  const proto::ProtoObject* const* args,
                                                  unsigned argc) {
    if (callee == PROTO_NONE) throw ScalaError("NullPointerException", "cannot call null");
    if (const BytecodeModule* m = compiledModule(ctx, callee)) {
        const proto::ProtoObject* caps =
            m->captureCount() ? callee->getOwnAttributeDirect(ctx, layout_.capturesKey) : nullptr;
        return execute(ctx, *m, args, argc, caps);
    }
    if (callee->isMethod(ctx))
        return callNative(ctx, callee->asMethod(ctx), callee->asMethodSelf(ctx), args, argc);
    return send(ctx, callee, layout_.applyName, args, argc);  // universal apply (DESIGN §5.1)
}

const proto::ProtoObject* ExecutionEngine::send(proto::ProtoContext* ctx,
                                                const proto::ProtoObject* receiver,
                                                const proto::ProtoString* name,
                                                const proto::ProtoObject* const* args,
                                                unsigned argc) {
    if (receiver == PROTO_NONE)
        throw ScalaError("NullPointerException",
                         "cannot invoke '" + name->toStdString(ctx) + "' on null");
    const proto::ProtoObject* m = receiver->getAttribute(ctx, name);
    if (!m || m == PROTO_NONE) {
        // PROTO_NONE is also a stored null: probe presence (DESIGN §4.1).
        if (receiver->hasAttribute(ctx, name) != PROTO_TRUE)
            throw ScalaError("NoSuchMethodError", "value " + name->toStdString(ctx) +
                             " is not a member of " + typeName(ctx, layout_, receiver));
        if (argc == 0) return PROTO_NONE;
        throw ScalaError("NullPointerException", "cannot call null");
    }
    if (m->isMethod(ctx)) return callNative(ctx, m->asMethod(ctx), receiver, args, argc);
    if (argc == 0 && !compiledModule(ctx, m)) return m;  // a plain attribute (field read)
    throw ScalaError("NoSuchMethodError",
                     "methods written in Scala on objects are not implemented yet");
}

const proto::ProtoObject* ExecutionEngine::force(proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* v) {
    if (v == PROTO_NONE || v->getPrototype(ctx) != layout_.lazyProto) return v;
    if (v->hasOwnAttribute(ctx, layout_.valueKey) == PROTO_TRUE)
        return v->getOwnAttributeDirect(ctx, layout_.valueKey);
    const proto::ProtoObject* thunk = v->getOwnAttributeDirect(ctx, layout_.thunkKey);
    const proto::ProtoObject* r = invoke(ctx, thunk, nullptr, 0);
    v->setAttribute(ctx, layout_.valueKey, r);  // mutable holder: in place
    return r;
}
```

The frame and dispatch loop:

```cpp
const proto::ProtoObject* ExecutionEngine::execute(proto::ProtoContext* parent,
                                                   const BytecodeModule& mod,
                                                   const proto::ProtoObject* const* args,
                                                   unsigned argc,
                                                   const proto::ProtoObject* captures) {
    checkNativeStack();
    const unsigned arity = static_cast<unsigned>(mod.arity());
    const unsigned fixed = mod.isVariadic() ? arity - 1 : arity;
    if (mod.isVariadic() ? argc < fixed : argc != arity)
        throw ScalaError("IllegalArgumentException",
                         "wrong number of arguments for " + mod.name() + ": expected " +
                         std::to_string(fixed) + (mod.isVariadic() ? " or more" : "") +
                         ", got " + std::to_string(argc));
    const unsigned stackBase = arity + static_cast<unsigned>(mod.localCount());

    proto::ProtoContext frame(parent->space, parent);
    frame.resizeAutomaticLocals(stackBase + static_cast<unsigned>(mod.maxStack()));
    const proto::ProtoObject** slots = frame.getAutomaticLocals();  // never resized again
    for (unsigned k = 0; k < fixed; ++k) slots[k] = args[k];
    if (mod.isVariadic())
        slots[fixed] = frame.newList(argc - fixed, args + fixed)->asObject(&frame);
    if (mod.captureCount() > 0) {
        const proto::ProtoList* caps = captures->asList(&frame);
        const auto& specs = mod.captureSpecs();
        for (std::size_t k = 0; k < specs.size(); ++k)
            slots[specs[k].localSlot] = caps->getAt(&frame, static_cast<int>(k));
    }

    const proto::ProtoObject** sp = slots + stackBase;  // next free operand slot
    const Instr* const code = mod.code().data();
    const Instr* ip = code;
    const RuntimeLayout& L = layout_;
    try {
        for (;;) {
            Instr word = *ip++;
            Op op = static_cast<Op>(word & 0xFF);
            std::uint64_t operand = word >> kOperandShift;
            if (op == Op::EXTEND) {
                const Instr next = *ip++;
                operand = (operand << 24) | (next >> kOperandShift);
                op = static_cast<Op>(next & 0xFF);
            }
            switch (op) {
                case Op::NOP: case Op::EXTEND: break;
                case Op::PUSH_CONST: {
                    const auto& c = mod.constAt(operand);
                    using K = BytecodeModule::ConstKind;
                    switch (c.kind) {
                        case K::Int:    *sp++ = frame.fromInteger(c.ival); break;
                        case K::BigInt: *sp++ = frame.fromString(c.sval.c_str(), c.base); break;
                        case K::Double: *sp++ = frame.fromDouble(c.dval); break;
                        case K::String: *sp++ = frame.fromUTF8String(c.sval.c_str()); break;
                        case K::Char:   *sp++ = frame.fromUnicodeChar(static_cast<unsigned>(c.ival)); break;
                        case K::Symbol: case K::SendSite:
                            throw std::logic_error("PUSH_CONST of a name constant");
                    }
                    break;
                }
                case Op::PUSH_UNIT:  *sp++ = L.unit; break;
                case Op::PUSH_NULL:  *sp++ = PROTO_NONE; break;
                case Op::PUSH_TRUE:  *sp++ = PROTO_TRUE; break;
                case Op::PUSH_FALSE: *sp++ = PROTO_FALSE; break;
                case Op::POP: --sp; break;
                case Op::DUP: *sp = sp[-1]; ++sp; break;
                case Op::PUSH_LOCAL:  *sp++ = slots[operand]; break;
                case Op::STORE_LOCAL: slots[operand] = *--sp; break;
                case Op::MAKE_CELL:
                    slots[operand] = L.cellProto->newChild(&frame, /*isMutable=*/true);
                    break;
                case Op::PUSH_CELL: {
                    const proto::ProtoObject* v = slots[operand]->getOwnAttributeDirect(&frame, L.valueKey);
                    *sp++ = v ? v : PROTO_NONE;
                    break;
                }
                case Op::STORE_CELL:
                    slots[operand]->setAttribute(&frame, L.valueKey, sp[-1]);  // mutable: in place
                    --sp;
                    break;
                case Op::PUSH_GLOBAL: {
                    const auto* key = mod.constAt(operand).symbol;
                    const proto::ProtoObject* v = L.globals->getOwnAttributeDirect(&frame, key);
                    if ((!v || v == PROTO_NONE) &&
                        L.globals->hasOwnAttribute(&frame, key) != PROTO_TRUE) [[unlikely]]
                        throw ScalaError("UninitializedFieldError",
                                         mod.constAt(operand).sval + " is used before it is initialised");
                    *sp++ = v ? v : PROTO_NONE;
                    break;
                }
                case Op::STORE_GLOBAL:
                    L.globals->setAttribute(&frame, mod.constAt(operand).symbol, sp[-1]);
                    --sp;
                    break;
                case Op::MAKE_FN: {
                    const BytecodeModule& sub = mod.block(operand);
                    const unsigned nc = static_cast<unsigned>(sub.captureCount());
                    const auto address = reinterpret_cast<std::intptr_t>(&sub);
                    // User-space addresses are below 2^47, inside the SmallInteger range.
                    const proto::ProtoObject* fn =
                        L.functionProtoFor(static_cast<unsigned>(sub.arity()))->newChild(&frame)
                            ->setAttribute(&frame, L.codeKey, proto::makeSmallInt(address));
                    if (nc > 0)
                        fn = fn->setAttribute(&frame, L.capturesKey,
                                              frame.newList(nc, sp - nc)->asObject(&frame));
                    sp -= nc;
                    *sp++ = fn;
                    break;
                }
                case Op::CALL: {
                    const proto::ProtoObject** base = sp - operand - 1;
                    const proto::ProtoObject* r =
                        invoke(&frame, base[0], base + 1, static_cast<unsigned>(operand));
                    base[0] = r;
                    sp = base + 1;
                    break;
                }
                case Op::CALL_SPREAD: {
                    const unsigned n = static_cast<unsigned>(operand);
                    const proto::ProtoObject** base = sp - n - 2;  // callee
                    const proto::ProtoObject* listObj = sp[-1];
                    if (!isListFast(listObj))
                        throw ScalaError("ClassCastException",
                                         typeName(&frame, L, listObj) + " cannot be spliced as arguments");
                    const proto::ProtoList* list = listObj->asList(&frame);
                    const unsigned extra = static_cast<unsigned>(list->getSize(&frame));
                    const proto::ProtoObject* r;
                    {
                        proto::ProtoContext argScope(frame.space, &frame);
                        argScope.resizeAutomaticLocals(n + extra);
                        const proto::ProtoObject** a = argScope.getAutomaticLocals();
                        for (unsigned k = 0; k < n; ++k) a[k] = base[1 + k];
                        for (unsigned k = 0; k < extra; ++k) a[n + k] = list->getAt(&argScope, static_cast<int>(k));
                        r = invoke(&argScope, base[0], a, n + extra);
                        argScope.returnValue = r;
                    }
                    base[0] = r;
                    sp = base + 1;
                    break;
                }
                case Op::SEND: {
                    const auto& site = mod.constAt(operand);
                    const proto::ProtoObject** base = sp - site.argc - 1;  // receiver
                    const proto::ProtoObject* r = send(&frame, base[0], site.symbol, base + 1, site.argc);
                    base[0] = r;
                    sp = base + 1;
                    break;
                }
                case Op::RETURN: {
                    const proto::ProtoObject* r = sp[-1];
                    frame.returnValue = r;
                    return r;
                }
                case Op::MAKE_LAZY: {
                    const proto::ProtoObject* holder = L.lazyProto->newChild(&frame, true);
                    holder->setAttribute(&frame, L.thunkKey, sp[-1]);  // mutable: in place
                    sp[-1] = holder;
                    break;
                }
                case Op::FORCE: sp[-1] = force(&frame, sp[-1]); break;
                case Op::JUMP: ip += operand; break;
                case Op::JUMP_IF_FALSE: {
                    const proto::ProtoObject* v = *--sp;
                    if (v == PROTO_FALSE) ip += operand;
                    else if (v != PROTO_TRUE) throwNotBoolean(&frame, L, v);
                    break;
                }
                case Op::JUMP_IF_TRUE: {
                    const proto::ProtoObject* v = *--sp;
                    if (v == PROTO_TRUE) ip += operand;
                    else if (v != PROTO_FALSE) throwNotBoolean(&frame, L, v);
                    break;
                }
                case Op::JUMP_BACK:
                    ip -= operand;
                    frame.safepoint();  // Open question Q21; every live value is in a slot
                    break;
                case Op::ADD: case Op::SUB: case Op::MUL: {
                    const proto::ProtoObject* a = sp[-2];
                    const proto::ProtoObject* b = sp[-1];
                    if (proto::isSmallInt(a) && proto::isSmallInt(b)) {
                        const long long x = proto::asSmallInt(a), y = proto::asSmallInt(b);
                        long long r;
                        bool fits;
                        if (op == Op::ADD)      { r = x + y; fits = proto::smallIntInRange(r); }
                        else if (op == Op::SUB) { r = x - y; fits = proto::smallIntInRange(r); }
                        else fits = !__builtin_mul_overflow(x, y, &r) && proto::smallIntInRange(r);
                        // |x|, |y| < 2^53, so x + y and x - y cannot overflow a long long.
                        sp[-2] = fits ? proto::makeSmallInt(r)
                               : op == Op::ADD ? a->add(&frame, b)        // promotes to LargeInteger
                               : op == Op::SUB ? a->subtract(&frame, b)
                                               : a->multiply(&frame, b);
                    } else {
                        sp[-2] = slowBinary(&frame, op, a, b);
                    }
                    --sp;
                    break;
                }
                case Op::LT: case Op::LE: case Op::GT: case Op::GE: {
                    const proto::ProtoObject* a = sp[-2];
                    const proto::ProtoObject* b = sp[-1];
                    if (proto::isSmallInt(a) && proto::isSmallInt(b)) {
                        const long long x = proto::asSmallInt(a), y = proto::asSmallInt(b);
                        const bool r = op == Op::LT ? x < y : op == Op::LE ? x <= y
                                     : op == Op::GT ? x > y : x >= y;
                        sp[-2] = r ? PROTO_TRUE : PROTO_FALSE;
                    } else {
                        sp[-2] = slowBinary(&frame, op, a, b);
                    }
                    --sp;
                    break;
                }
                case Op::EQ: case Op::NE: {
                    const bool eq = valuesEqual(&frame, L, sp[-2], sp[-1]);
                    sp[-2] = (eq == (op == Op::EQ)) ? PROTO_TRUE : PROTO_FALSE;
                    --sp;
                    break;
                }
                case Op::NEG: {
                    const proto::ProtoObject* a = sp[-1];
                    if (proto::isSmallInt(a) && proto::asSmallInt(a) != proto::PROTO_SMALL_INT_MIN)
                        sp[-1] = proto::makeSmallInt(-proto::asSmallInt(a));
                    else if (isNumberFast(a))
                        sp[-1] = a->negate(&frame);
                    else
                        sp[-1] = send(&frame, a,
                                      proto::ProtoString::createSymbol(&frame, "unary_-"), nullptr, 0);
                    break;
                }
                case Op::NOT: {
                    const proto::ProtoObject* a = sp[-1];
                    if (a == PROTO_TRUE) sp[-1] = PROTO_FALSE;
                    else if (a == PROTO_FALSE) sp[-1] = PROTO_TRUE;
                    else sp[-1] = send(&frame, a,
                                       proto::ProtoString::createSymbol(&frame, "unary_!"), nullptr, 0);
                    break;
                }
            }
        }
    } catch (ScalaError& e) {
        if (e.line == 0) e.line = mod.lineAt(static_cast<std::size_t>(ip - code) - 1);
        throw;
    } catch (const std::runtime_error& e) {
        // A protoCore error (e.g. "Objects are not integer types for division.")
        // becomes a Scala RuntimeException; it is never swallowed (DESIGN §7).
        ScalaError se("RuntimeException", e.what());
        se.line = mod.lineAt(static_cast<std::size_t>(ip - code) - 1);
        throw se;
    }
}
```

(`createSymbol(&frame, "unary_-")` on the rare non-number path is a hash lookup, not an
allocation per call after the first; it is not cached in a function-local static because
symbols are per space — DESIGN §9. Write `switch (op)` without `default:` so a new opcode
without a case is a warning.)

`slowBinary(ctx, op, a, b)` — the operands are still in the caller's slots:

```cpp
const proto::ProtoObject* ExecutionEngine::slowBinary(proto::ProtoContext* ctx, Op op,
                                                      const proto::ProtoObject* a,
                                                      const proto::ProtoObject* b) {
    const RuntimeLayout& L = layout_;
    // String concatenation: either side a String (Scala String.+ / Int.+(String)).
    if (op == Op::ADD && (proto::ProtoObject::isStringTagFast(a) || proto::ProtoObject::isStringTagFast(b))) {
        const proto::ProtoObject* sa = toScalaString(ctx, L, a);
        const proto::ProtoObject* sb = toScalaString(ctx, L, b);
        return reinterpret_cast<const proto::ProtoString*>(sa)
            ->appendLast(ctx, reinterpret_cast<const proto::ProtoString*>(sb))->asObject(ctx);
    }
    // "ab" * 3
    if (op == Op::MUL && proto::ProtoObject::isStringTagFast(a) && isIntegerFast(b))
        return a->multiply(ctx, b);
    // Numbers, with Char promoted to its code point (Scala Char arithmetic yields Int).
    auto numeric = [&](const proto::ProtoObject* v) {
        return isCharFast(v) ? ctx->fromInteger(static_cast<long long>(charValueFast(v))) : v;
    };
    const proto::ProtoObject* x = numeric(a);
    const proto::ProtoObject* y = numeric(b);
    if (isNumberFast(x) && isNumberFast(y)) {
        switch (op) {
            case Op::ADD: return x->add(ctx, y);
            case Op::SUB: return x->subtract(ctx, y);
            case Op::MUL: return x->multiply(ctx, y);
            default: break;
        }
        const auto c = x->partialCompare(ctx, y);  // IEEE: NaN compares false
        const bool r = op == Op::LT ? c < 0 : op == Op::LE ? c <= 0 : op == Op::GT ? c > 0 : c >= 0;
        return r ? PROTO_TRUE : PROTO_FALSE;
    }
    if (op != Op::ADD && op != Op::SUB && op != Op::MUL &&
        proto::ProtoObject::isStringTagFast(a) && proto::ProtoObject::isStringTagFast(b)) {
        const auto c = a->partialCompare(ctx, b);
        const bool r = op == Op::LT ? c < 0 : op == Op::LE ? c <= 0 : op == Op::GT ? c > 0 : c >= 0;
        return r ? PROTO_TRUE : PROTO_FALSE;
    }
    // Anything else is an ordinary method call on the left operand.
    const proto::ProtoObject* argv[1] = {b};
    return send(ctx, a, proto::ProtoString::createSymbol(ctx, opSymbol(op)), argv, 1);
}
```

`CMakeLists.txt`:

```cmake
# --- Runtime: VM, values, primitives, stack guard --------------------------------
add_library(protoscala_runtime STATIC
    src/runtime/StackGuard.cpp
    src/runtime/Runtime.cpp
    src/runtime/Values.cpp
    src/runtime/ExecutionEngine.cpp
)
target_link_libraries(protoscala_runtime PUBLIC protoscala_compiler protoscala_support
    ${PROTOCORE_LIBRARY} pthread)
target_compile_options(protoscala_runtime PRIVATE -Wall -Wextra -Wpedantic)
```

Unit tests: add `test_values.cpp`, `test_stackguard.cpp`, `test_engine.cpp`; link
`protoscala_runtime`.

- [ ] **Step 8: Run to verify pass**

Run: `cmake --build build_release && ctest --test-dir build_release -R "Values|StackGuard|Engine" --output-on-failure`
Expected: all PASS. If `Engine.RuntimeErrors` fails on `1.noSuchMethod`, check that
`send` probes with `hasAttribute` after a `PROTO_NONE` result.

- [ ] **Step 9: Commit**

```bash
git add CMakeLists.txt src/runtime tests/unit
git commit -m "runtime: layout, values, stack guard and recursive bytecode VM

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```


---

### Task 9: Primitives

**Files:**
- Create: `src/runtime/Primitives.h`, `src/runtime/Primitives.cpp`, `tests/unit/test_primitives.cpp`
- Modify: `CMakeLists.txt` (add `Primitives.cpp` to `protoscala_runtime`), `tests/unit/EvalHarness.h` (install primitives), `tests/unit/CMakeLists.txt`

**Interfaces:**
- Consumes: `RuntimeLayout`, `ExecutionEngine::invoke`, `activeCallContext`, `ScalaError`, `Values.h` (Task 8).
- Produces:
  - `void installPrimitives(proto::ProtoContext* ctx, const RuntimeLayout& layout);`
  - `const std::vector<std::string>& builtinGlobalNames();` — `{"println", "print"}`; the session declares them as `BindingKind::Builtin`.

Every primitive has protoCore's `ProtoMethod` signature
`(ctx, self, parentLink, positionalArgs, keywordArgs)` and is installed with
`ctx->fromMethod(nullptr, fn)` under an interned name on a (mutable) protoScala prototype,
exactly as protoClojure's `installPrimitives` (`src/runtime/Primitives.cpp:2805-2817`).
`self` is the receiver (the VM passes it). Errors are `ScalaError`s with Scala's class
names and messages.

| Prototype | Methods (Phase 1) |
|---|---|
| globals | `println()`, `println(x)`, `print(x)` — write `show(x)` (+ `\n`) with one `fwrite` to stdout; no per-call flush (the session flushes before any stderr output and at exit) |
| Any | `toString`, `equals(x)` (`valuesEqual`), `==`, `!=`, `eq`, `ne` (identity) |
| Int | `+ - * / %` (`/` and `%` raise `ArithmeticException: / by zero`), `< <= > >=`, `unary_- unary_+ unary_~`, `& \| ^ << >>` (protoCore bitwise), `>>>` (raises `UnsupportedOperationException: >>> is not supported: integers have no fixed width (D1)`, Open question Q12), `abs max min`, `toInt toLong toDouble toFloat toChar` |
| Double | `+ - * / %` (`%` = `std::fmod`), `< <= > >=`, `unary_- unary_+`, `abs max min round floor ceil isNaN isInfinite`, `toInt toLong` (truncate toward zero; D1: no clamping, NaN → 0), `toDouble toFloat` |
| Boolean | `& \| ^ unary_!` |
| Char | `+ - < <= > >=` (via the code point; results are Int), `toInt toLong toDouble toChar isDigit isLetter isWhitespace isUpper isLower toUpper toLower` (ASCII case mapping, Open question Q11) |
| String | `length isEmpty nonEmpty charAt apply substring(start) substring(start, end) toUpperCase toLowerCase trim contains startsWith endsWith indexOf reverse * + concat toInt toDouble` (`toInt` raises `NumberFormatException: For input string: "x"`; indices out of range raise `StringIndexOutOfBoundsException`; lengths and indices count code points, Open question Q11) |
| List | `length size isEmpty nonEmpty apply(i) head foreach(f) mkString mkString(sep) mkString(start, sep, end)` (`apply` out of range: `IndexOutOfBoundsException`; `head` of empty: `NoSuchElementException: head of empty list`) |
| Function (Function0..22, native methods) | `apply(args...)` — re-enters the VM through `activeCallContext()->engine->invoke` |

- [ ] **Step 1: Write the failing tests**

Modify `tests/unit/EvalHarness.h`: include `runtime/Primitives.h` and make the constructor

```cpp
    EvalHarness() : runtime_(space_), engine_(runtime_.layout()) {
        installPrimitives(runtime_.rootContext(), runtime_.layout());
        for (const auto& n : builtinGlobalNames()) globals_.declare(n, BindingKind::Builtin);
    }
```

`tests/unit/test_primitives.cpp`:

```cpp
#include "EvalHarness.h"

#include <gtest/gtest.h>

using protoScala::test::EvalHarness;

namespace {
bool isError(const std::string& r, const std::string& cls) {
    return r.rfind("error: " + cls, 0) == 0;
}
}

TEST(Primitives, IntMethods) {
    EvalHarness h;
    EXPECT_EQ(h.eval("1.toString + 2"), "12");
    EXPECT_EQ(h.eval("7 / 2"), "3");
    EXPECT_EQ(h.eval("-7 / 2"), "-3");
    EXPECT_EQ(h.eval("-7 % 2"), "-1");
    EXPECT_EQ(h.eval("7 % -2"), "1");
    EXPECT_EQ(h.eval("3.max(7)"), "7");
    EXPECT_EQ(h.eval("-3.abs"), "3");
    EXPECT_EQ(h.eval("10.toDouble"), "10.0");
    EXPECT_EQ(h.eval("98.toChar"), "b");
    EXPECT_EQ(h.eval("6 & 3"), "2");
    EXPECT_EQ(h.eval("1 << 60"), "1152921504606846976");
    EXPECT_EQ(h.eval("1 / 0"), "error: ArithmeticException: / by zero");
    EXPECT_TRUE(isError(h.eval("8 >>> 1"), "UnsupportedOperationException"));
}

TEST(Primitives, DoubleMethods) {
    EvalHarness h;
    EXPECT_EQ(h.eval("7.0 / 2"), "3.5");
    EXPECT_EQ(h.eval("5.5 % 2"), "1.5");
    EXPECT_EQ(h.eval("1.0 / 0"), "Infinity");
    EXPECT_EQ(h.eval("(0.0 / 0.0).isNaN"), "true");
    EXPECT_EQ(h.eval("3.7.toInt"), "3");
    EXPECT_EQ(h.eval("-3.7.toInt"), "-3");
    EXPECT_EQ(h.eval("2.5.round"), "3");
    EXPECT_EQ(h.eval("2.5.floor"), "2.0");
}

TEST(Primitives, CharAndBoolean) {
    EvalHarness h;
    EXPECT_EQ(h.eval("'a'.toInt"), "97");
    EXPECT_EQ(h.eval("'7'.isDigit"), "true");
    EXPECT_EQ(h.eval("'q'.toUpper"), "Q");
    EXPECT_EQ(h.eval("true & false"), "false");
    EXPECT_EQ(h.eval("true ^ true"), "false");
}

TEST(Primitives, StringMethods) {
    EvalHarness h;
    EXPECT_EQ(h.eval("\"hello\".length"), "5");
    EXPECT_EQ(h.eval("\"hello\".toUpperCase"), "HELLO");
    EXPECT_EQ(h.eval("\"hello\".substring(1, 3)"), "el");
    EXPECT_EQ(h.eval("\"hello\".substring(3)"), "lo");
    EXPECT_EQ(h.eval("\"hello\"(1)"), "e");
    EXPECT_EQ(h.eval("\"hello\".charAt(0)"), "h");
    EXPECT_EQ(h.eval("\"ab\" * 3"), "ababab");
    EXPECT_EQ(h.eval("\"  x \".trim"), "x");
    EXPECT_EQ(h.eval("\"hello\".indexOf(\"ll\")"), "2");
    EXPECT_EQ(h.eval("\"hello\".contains(\"ell\")"), "true");
    EXPECT_EQ(h.eval("\"42\".toInt + 1"), "43");
    EXPECT_EQ(h.eval("\"abc\".reverse"), "cba");
    EXPECT_EQ(h.eval("\"\".isEmpty"), "true");
    EXPECT_EQ(h.eval("\"año\".length"), "3");
    EXPECT_TRUE(isError(h.eval("\"x\".toInt"), "NumberFormatException"));
    EXPECT_TRUE(isError(h.eval("\"abc\".charAt(5)"), "StringIndexOutOfBoundsException"));
}

TEST(Primitives, ListMethodsAndReentry) {
    EvalHarness h;
    EXPECT_EQ(h.eval("def l(xs: Int*) = xs"), "");
    EXPECT_EQ(h.eval("l(1, 2, 3).length"), "3");
    EXPECT_EQ(h.eval("l(5, 6)(1)"), "6");
    EXPECT_EQ(h.eval("l(1, 2, 3).mkString(\"-\")"), "1-2-3");
    EXPECT_EQ(h.eval("l(1, 2, 3).mkString(\"[\", \",\", \"]\")"), "[1,2,3]");
    EXPECT_EQ(h.eval("l(4).head"), "4");
    EXPECT_TRUE(isError(h.eval("l().head"), "NoSuchElementException"));
    EXPECT_TRUE(isError(h.eval("l(1)(3)"), "IndexOutOfBoundsException"));
    EXPECT_EQ(h.eval("{ var s = 0; l(1, 2, 3).foreach(x => s += x); s }"), "6");
    // Nested re-entry: a primitive calls a lambda that calls a primitive.
    EXPECT_EQ(h.eval("{ var s = 0; l(1, 2).foreach(x => l(10, 20).foreach(y => s += x * y)); s }"),
              "90");
}

TEST(Primitives, FunctionApplyAndAny) {
    EvalHarness h;
    EXPECT_EQ(h.eval("((x: Int) => x * x).apply(7)"), "49");
    EXPECT_EQ(h.eval("\"a\".equals(\"a\")"), "true");
    EXPECT_EQ(h.eval("(1 == 1).toString"), "true");
    EXPECT_EQ(h.eval("1.foo"), "error: NoSuchMethodError: value foo is not a member of Int");
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build_release`
Expected: FAIL — `runtime/Primitives.h: No such file or directory`.

- [ ] **Step 3: Write `Primitives.h`**

```cpp
/*
 * Primitives — native methods of the Phase 1 standard surface, installed on
 * the protoScala prototypes and the globals object (see the table in the
 * Phase 1 plan). Each matches protoCore's ProtoMethod signature; `self` is
 * the receiver.
 */
#pragma once
#include "runtime/Runtime.h"

#include <string>
#include <vector>

namespace proto { class ProtoContext; }

namespace protoScala {

void installPrimitives(proto::ProtoContext* ctx, const RuntimeLayout& layout);

// Global functions installed by installPrimitives ({"println", "print"}).
const std::vector<std::string>& builtinGlobalNames();

} // namespace protoScala
```

- [ ] **Step 4: Write `Primitives.cpp`**

Shared helpers and representative primitives (write every method of the table in the same
style):

```cpp
#include "runtime/Primitives.h"
#include "runtime/Errors.h"
#include "runtime/ExecutionEngine.h"
#include "runtime/Values.h"
#include "protoCore.h"

#include <cctype>
#include <cmath>
#include <cstdio>

namespace protoScala {

namespace {

using proto::ProtoContext;
using proto::ProtoObject;
using proto::ProtoList;

#define PRIM(name) \
    const ProtoObject* name(ProtoContext* ctx, const ProtoObject* self, \
                            const proto::ParentLink*, const ProtoList* args, \
                            const proto::ProtoSparseList*)

const RuntimeLayout& layoutOf() { return *activeCallContext()->layout; }

unsigned long argCount(ProtoContext* ctx, const ProtoList* args) {
    return args ? args->getSize(ctx) : 0;
}

const ProtoObject* arg(ProtoContext* ctx, const ProtoList* args, unsigned long i,
                       const char* method, unsigned long expected) {
    if (argCount(ctx, args) != expected)
        throw ScalaError("IllegalArgumentException",
                         std::string(method) + " takes " + std::to_string(expected) +
                         " argument(s), got " + std::to_string(argCount(ctx, args)));
    return args->getAt(ctx, static_cast<int>(i));
}

long long intArg(ProtoContext* ctx, const ProtoObject* v, const char* method) {
    if (proto::isSmallInt(v)) return proto::asSmallInt(v);
    if (isLargeIntFast(v)) return v->asLong(ctx);  // throws std::overflow_error beyond 64 bits
    throw ScalaError("ClassCastException", std::string(method) + " expects an Int, got " +
                     typeName(ctx, layoutOf(), v));
}

const ProtoObject* boolean(bool b) { return b ? PROTO_TRUE : PROTO_FALSE; }

const ProtoObject* str(ProtoContext* ctx, const std::string& s) {
    return ctx->fromUTF8String(s.c_str());
}

// --- globals ---------------------------------------------------------------

PRIM(prim_println) {
    (void)self;
    std::string line;
    const unsigned long n = argCount(ctx, args);
    if (n > 1) throw ScalaError("IllegalArgumentException", "println takes 0 or 1 arguments");
    if (n == 1) line = show(ctx, layoutOf(), args->getAt(ctx, 0));
    line += '\n';
    std::fwrite(line.data(), 1, line.size(), stdout);
    return layoutOf().unit;
}

PRIM(prim_print) {
    (void)self;
    const std::string s = show(ctx, layoutOf(), arg(ctx, args, 0, "print", 1));
    std::fwrite(s.data(), 1, s.size(), stdout);
    return layoutOf().unit;
}

// --- Any -------------------------------------------------------------------

PRIM(any_toString) { (void)args; return str(ctx, show(ctx, layoutOf(), self)); }
PRIM(any_equals)   { return boolean(valuesEqual(ctx, layoutOf(), self, arg(ctx, args, 0, "equals", 1))); }
PRIM(any_eq)       { return boolean(self == arg(ctx, args, 0, "eq", 1)); }
PRIM(any_ne)       { return boolean(self != arg(ctx, args, 0, "ne", 1)); }

// --- Int -------------------------------------------------------------------

[[noreturn]] void divisionByZero() { throw ScalaError("ArithmeticException", "/ by zero"); }

PRIM(int_div) {
    const ProtoObject* d = arg(ctx, args, 0, "/", 1);
    if (isDoubleFast(d)) return self->divide(ctx, d);
    if (proto::isSmallInt(d) && proto::asSmallInt(d) == 0) divisionByZero();
    if (isCharFast(d)) d = ctx->fromInteger(charValueFast(d));
    return self->divide(ctx, d);  // Integer::divide truncates toward zero (Java /)
}

PRIM(int_mod) {
    const ProtoObject* d = arg(ctx, args, 0, "%", 1);
    if (isDoubleFast(d)) return ctx->fromDouble(std::fmod(self->asDouble(ctx), d->asDouble(ctx)));
    if (proto::isSmallInt(d) && proto::asSmallInt(d) == 0) divisionByZero();
    return self->modulo(ctx, d);  // Integer::modulo: sign of the dividend (Java %)
}

PRIM(int_ushr) {
    (void)ctx; (void)self; (void)args;
    throw ScalaError("UnsupportedOperationException",
                     ">>> is not supported: integers have no fixed width (D1)");
}

// --- String ----------------------------------------------------------------

const proto::ProtoString* asStr(const ProtoObject* v) {
    return reinterpret_cast<const proto::ProtoString*>(v);
}

PRIM(string_length) { (void)args; return ctx->fromInteger(static_cast<long long>(asStr(self)->getSize(ctx))); }

PRIM(string_charAt) {
    const long long i = intArg(ctx, arg(ctx, args, 0, "charAt", 1), "charAt");
    const long long n = static_cast<long long>(asStr(self)->getSize(ctx));
    if (i < 0 || i >= n)
        throw ScalaError("StringIndexOutOfBoundsException",
                         "index " + std::to_string(i) + ", length " + std::to_string(n));
    return asStr(self)->getAt(ctx, static_cast<int>(i));  // a Char (fromUnicodeChar)
}

PRIM(string_toInt) {
    (void)args;
    const std::string s = asStr(self)->toStdString(ctx);
    const bool ok = !s.empty() &&
        s.find_first_not_of("0123456789", (s[0] == '-' || s[0] == '+') ? 1 : 0) == std::string::npos &&
        s.size() > ((s[0] == '-' || s[0] == '+') ? 1u : 0u);
    if (!ok) throw ScalaError("NumberFormatException", "For input string: \"" + s + "\"");
    return ctx->fromString(s[0] == '+' ? s.c_str() + 1 : s.c_str(), 10);
}

// --- List ------------------------------------------------------------------

PRIM(list_foreach) {
    const ActiveCallContext* cc = activeCallContext();
    const ProtoObject* f = arg(ctx, args, 0, "foreach", 1);  // rooted: in `args`
    const ProtoList* list = self->asList(ctx);                // rooted: the receiver
    const unsigned long n = list->getSize(ctx);
    for (unsigned long i = 0; i < n; ++i) {
        // One short-lived context per element: each call's garbage is released
        // when the step ends (protoClojure src/runtime/ListBuilder.h:12-40).
        ProtoContext step(ctx->space, ctx);
        step.resizeAutomaticLocals(1);
        step.setAutomaticLocal(0, list->getAt(&step, static_cast<int>(i)));
        cc->engine->invoke(&step, f, step.getAutomaticLocals(), 1);
    }
    return cc->layout->unit;
}

// --- Function --------------------------------------------------------------

PRIM(function_apply) {
    const ActiveCallContext* cc = activeCallContext();
    const unsigned long n = argCount(ctx, args);
    ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(static_cast<unsigned>(n));
    for (unsigned long i = 0; i < n; ++i)
        scope.setAutomaticLocal(static_cast<unsigned>(i), args->getAt(&scope, static_cast<int>(i)));
    const ProtoObject* r = cc->engine->invoke(&scope, self, scope.getAutomaticLocals(),
                                              static_cast<unsigned>(n));
    scope.returnValue = r;
    return r;
}

struct MethodEntry {
    const char* name;
    proto::ProtoMethod fn;
};

void installAll(ProtoContext* ctx, proto::ProtoObject* target,
                const MethodEntry* entries, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i)
        target->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, entries[i].name),
                             ctx->fromMethod(nullptr, entries[i].fn));
}

} // namespace

const std::vector<std::string>& builtinGlobalNames() {
    static const std::vector<std::string> names = {"println", "print"};  // plain strings
    return names;
}

void installPrimitives(ProtoContext* ctx, const RuntimeLayout& L) {
    static constexpr MethodEntry globals[] = {{"println", &prim_println}, {"print", &prim_print}};
    static constexpr MethodEntry any[] = {
        {"toString", &any_toString}, {"equals", &any_equals}, {"==", &any_equals},
        {"eq", &any_eq}, {"ne", &any_ne} /* "!=" via a small any_notEquals */};
    static constexpr MethodEntry ints[] = {
        {"/", &int_div}, {"%", &int_mod}, {">>>", &int_ushr} /* + the rest of the table */};
    static constexpr MethodEntry lists[] = {{"foreach", &list_foreach} /* + the rest */};
    static constexpr MethodEntry functions[] = {{"apply", &function_apply}};
    installAll(ctx, L.globals, globals, std::size(globals));
    installAll(ctx, L.anyProto, any, std::size(any));
    installAll(ctx, L.intProto, ints, std::size(ints));
    installAll(ctx, L.listProto, lists, std::size(lists));
    installAll(ctx, L.functionProto, functions, std::size(functions));
    // ... doubleProto, booleanProto, charProto, stringProto likewise (every row of the table).
}

} // namespace protoScala
```

The primitives run while the `ActiveCallContext` installed by `ExecutionEngine::run` is
present (every VM entry goes through `run`), so `layoutOf()` is always valid inside a
primitive; `installPrimitives` itself runs before any VM entry and uses its `L` argument.
The comments `/* + the rest of the table */` mark where the remaining entries of the
table at the top of this task are listed — write all of them; none is optional.

Implementation notes for the remaining methods:

- Int `+ - * < <= > >=` natives exist for explicit calls (`1.+(2)`) and for mixed operands
  the VM's slow path hands over: numeric operands (Char via code point) use protoCore
  `add/subtract/multiply/partialCompare`; a String right operand of `+` concatenates;
  anything else raises `ClassCastException: <op> expects a number, got <Type>`.
- Int `& | ^` → `bitwiseAnd/Or/Xor`; `<< >>` → `shiftLeft/shiftRight(ctx, int)`;
  `unary_~` → `bitwiseNot`; `abs` → `ProtoObject::abs`; `max/min` via `partialCompare`;
  `toDouble` → `fromDouble(asDouble)`; `toChar` → `fromUnicodeChar`.
- Double `toInt`/`toLong`: NaN → 0; `std::trunc`, then `fromInteger` when it fits a
  `long long`, else `fromString(<"%.0f" digits>, 10)`. `round` → `floor(d + 0.5)` as an
  integer (Java `Math.round`); `floor`/`ceil` return Doubles.
- String `substring`, `indexOf`, `contains`, `startsWith`, `endsWith`, `reverse`, `trim`,
  `toUpperCase`/`toLowerCase` work on `toStdString` and code-point indices (decode the UTF-8
  once per call); results are new strings via `fromUTF8String`. `*` → `self->multiply`.
  `+`/`concat` → `appendLast` of `toScalaString(arg)`. `apply(i)` = `charAt(i)`.
- List `apply(i)` / `head` → `getAt` after the bounds check; `mkString` renders each element
  with `show` (strings bare, as Scala does).

- [ ] **Step 5: Run to verify pass**

Run: `cmake --build build_release && ctest --test-dir build_release -R "Primitives|Engine" --output-on-failure`
Expected: all PASS (the engine tests still pass with primitives installed).

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/runtime tests/unit
git commit -m "runtime: primitives for Int, Double, Boolean, Char, String, List, Function

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```


---

### Task 10: Session, script runner and the Phase 1 conformance suite

**Files:**
- Create: `src/repl/Session.h`, `src/repl/Session.cpp`
- Modify: `src/main.cpp`, `CMakeLists.txt` (add `protoscala_repl` with `Session.cpp`; link the binary), `tests/CMakeLists.txt`
- Modify: `tests/conformance/00-binary/hello-world.scala` (XFAIL → EXPECT)
- Create: fixtures under `tests/conformance/01-lexical/`, `02-expressions/`, `03-definitions/`, `04-control/`, `05-functions/`, `06-recursion/` (listed in Step 1)
- Create: `tests/cli/script-errors.sh`, `tests/cli/main-args.sh`, `tests/cli/disassemble.sh`, `tests/cli/gc-pressure.sh`

**Interfaces:**
- Consumes: everything of Tasks 1–9.
- Produces:
  - `enum class EvalStatus { Ok, Incomplete, Error };`
  - `struct EvalOutcome { EvalStatus status = EvalStatus::Ok; std::vector<std::string> echo; };`
  - `class Session { Session(); int runScript(const std::string& path, const std::vector<std::string>& args); EvalOutcome evalReplInput(const std::string& source); bool loadFile(const std::string& path); int disassemble(const std::string& path); }`

Behaviour:

- **Pipeline per unit:** `parseSource` → `desugar` → `Compiler::compileUnit` against a *copy*
  of the session's `GlobalTable` (committed only when compilation succeeds, so a failed REPL
  input declares nothing) → `linkSymbols` → retain the module → `ExecutionEngine::run` →
  `@main` call (script and `:load`).
- **Error output** (stdout is flushed first; exit code 1 for scripts; Open question Q9):
  parse/compile: `<file>:<line>:<col>: error: <message>`;
  runtime: `<file>:<line>: error: <Class>: <message>`; REPL inputs use the file name
  `<console>`. A missing file: `protoscala: cannot open '<path>'`.
- **@main:** looked up in the globals after the top-level code ran; called with no
  arguments, or with the command-line arguments after the script path as Strings when it
  takes `args: String*`.
- **REPL echo** (Open question Q10): for each definition of the input, `val x = <value>`,
  `var x = <value>`, `lazy val x` (not forced), `def f`; then, when the last statement is an
  expression whose value is not `()`, `val res<N> = <value>`.

- [ ] **Step 1: Write the failing fixtures and CLI tests**

Flip `tests/conformance/00-binary/hello-world.scala` to:

```scala
// EXPECT: Hello, protoScala!
// The first protoScala program.
@main def hello(): Unit =
  println("Hello, protoScala!")
```

Create the fixtures below (file name → full content). Where the two syntaxes differ, both
variants exist (`-indent` / `-braces`). Expected values were computed by hand against Scala
3 semantics; lines commented `D<n>` document a deviation.

`01-lexical/int-literals.scala`
```scala
// EXPECT: 42 31 5 1000000 7 -8
@main def run(): Unit =
  println(42.toString + " " + 0x1F + " " + 0b101 + " " + 1_000_000 + " " + 7L + " " + -8)
```

`01-lexical/float-literals.scala`
```scala
// EXPECT: 1.5 1000.0 0.0025 0.5 3.0 1.0E21 1.0E-5
// 3f is a Float in Scala, a Double here (D2); both print 3.0.
@main def run(): Unit =
  println(1.5.toString + " " + 1e3 + " " + 2.5e-3 + " " + .5 + " " + 3f + " " + 1e21 + " " + 0.00001)
```

`01-lexical/char-literals.scala`
```scala
// EXPECT: a|A|98|9
@main def run(): Unit =
  val tab = '\t'
  println("" + 'a' + '|' + 'A' + '|' + ('a' + 1) + '|' + tab.toInt)
```

`01-lexical/string-escapes.scala`
```scala
// EXPECT: 3 quote["] backslash[\] e-acute[é]
@main def run(): Unit =
  println("a\tb".length.toString + " quote[\"] backslash[\\] e-acute[é]")
```

`01-lexical/triple-quoted.scala`
```scala
// EXPECT: 4 a\nb 3
@main def run(): Unit =
  val raw = """a\nb"""
  val multi = """x
y"""
  println(raw.length.toString + " " + raw + " " + multi.length)
```

`01-lexical/nested-comments.scala`
```scala
// EXPECT: 3
/* outer /* inner */ still a comment */
@main def run(): Unit =
  // a line comment
  println(1 + /* inline */ 2)
```

`01-lexical/backquoted-identifiers.scala`
```scala
// EXPECT: 5
@main def run(): Unit =
  val `type` = 2
  val `my value` = 3
  println(`type` + `my value`)
```

`01-lexical/operator-identifiers.scala`
```scala
// EXPECT: 10 x_+ ok
def +++(a: Int, b: Int): Int = a * 2 + b * 2
def x_+(s: String): String = s + " ok"
@main def run(): Unit =
  println(+++(2, 3).toString + " " + x_+("x_+"))
```

`01-lexical/unicode-identifiers.scala`
```scala
// EXPECT: 6
@main def run(): Unit =
  val año = 2
  val número = 4
  println(año + número)
```

`01-lexical/string-interpolation.scala`
```scala
// XFAIL: Hello, Ada!
// Interpolated strings are lexed as structured tokens; evaluation comes later.
@main def run(): Unit =
  val name = "Ada"
  println(s"Hello, $name!")
```

`02-expressions/arithmetic-precedence.scala`
```scala
// EXPECT: 12 20
@main def run(): Unit =
  println((2 + 3 * 4 - 10 / 2 % 3).toString + " " + (2 + 3) * 4)
```

`02-expressions/integer-promotion.scala`
```scala
// EXPECT: 9007199254740992 9223372036854775808 15511210043330985984000000
// D1: Int and Long never wrap around. Scala prints -9223372036854775808 for the
// second value; protoScala promotes to arbitrary precision.
def fact(n: Int): BigInt = if n <= 1 then 1 else n * fact(n - 1)
@main def run(): Unit =
  val a = 9007199254740991L + 1
  val b = 9223372036854775807L + 1
  println(a.toString + " " + b + " " + fact(25))
```

`02-expressions/division-and-remainder.scala`
```scala
// EXPECT: -3 -1 -3 1 3.5 1.5
@main def run(): Unit =
  println((-7 / 2).toString + " " + (-7 % 2) + " " + (7 / -2) + " " + (7 % -2) + " " + 7.0 / 2 + " " + 5.5 % 2)
```

`02-expressions/division-by-zero.scala`
```scala
// EXPECT-ERROR: ArithmeticException: / by zero
@main def run(): Unit =
  val zero = 0
  println(10 / zero)
```

`02-expressions/double-formatting.scala`
```scala
// EXPECT: 0.30000000000000004 100.0 1.0E7 0.3333333333333333 -0.0
@main def run(): Unit =
  println((0.1 + 0.2).toString + " " + 100.0 + " " + 1.0e7 + " " + 1.0 / 3 + " " + -0.0)
```

`02-expressions/string-concatenation.scala`
```scala
// EXPECT: a12 3a xtrue cd
@main def run(): Unit =
  println(("a" + 1 + 2) + " " + (1 + 2 + "a") + " " + ("x" + true) + " " + ("c" + 'd'))
```

`02-expressions/string-methods.scala`
```scala
// EXPECT: 12 HELLO, WORLD Hello W 7 true ababab x true 42
@main def run(): Unit =
  val s = "Hello, World"
  println(s.length.toString + " " + s.toUpperCase + " " + s.substring(0, 5) + " " + s(7) +
    " " + s.indexOf("World") + " " + s.contains("lo") + " " + ("ab" * 3) + " " + "  x ".trim +
    " " + s.startsWith("Hell") + " " + "42".toInt)
```

`02-expressions/boolean-short-circuit.scala`
```scala
// EXPECT: false true side=0
@main def run(): Unit =
  var side = 0
  def touch(): Boolean = { side += 1; true }
  val a = false && touch()
  val b = true || touch()
  println(a.toString + " " + b + " side=" + side)
```

`02-expressions/equality.scala`
```scala
// EXPECT: true true true true true false
@main def run(): Unit =
  val nan = 0.0 / 0.0
  println((1 == 1.0).toString + " " + ("ab" == "a" + "b") + " " + (1 != 2) + " " +
    ('a' == 97) + " " + (null == null) + " " + (nan == nan))
```

`02-expressions/leading-infix-indent.scala`
```scala
// EXPECT: 10
@main def run(): Unit =
  val total = 1
  + 2
  + 3
  + 4
  println(total)
```

`02-expressions/leading-infix-braces.scala`
```scala
// EXPECT: 10
@main def run(): Unit = {
  val total = 1 +
    2 +
    3 +
    4
  println(total)
}
```

`03-definitions/val-var-indent.scala`
```scala
// EXPECT: 3 x=11
@main def run(): Unit =
  val a = 1
  val b: Int = 2
  var x = 10
  x = x + 1
  println((a + b).toString + " x=" + x)
```

`03-definitions/val-var-braces.scala`
```scala
// EXPECT: 3 x=11
@main def run(): Unit = {
  val a = 1
  val b: Int = 2
  var x = 10
  x = x + 1
  println((a + b).toString + " x=" + x)
}
```

`03-definitions/reassign-val.scala`
```scala
// EXPECT-ERROR: Reassignment to val a
@main def run(): Unit =
  val a = 1
  a = 2
```

`03-definitions/not-found.scala`
```scala
// EXPECT-ERROR: Not found: y
@main def run(): Unit =
  println(y)
```

`03-definitions/lazy-val.scala`
```scala
// EXPECT: before init 42 42
@main def run(): Unit =
  var log = "before"
  lazy val answer = { log = log + " init"; 42 }
  val first = answer
  val second = answer
  println(log + " " + first + " " + second)
```

`03-definitions/top-level-forward-reference.scala`
```scala
// EXPECT: 7
@main def run(): Unit = println(helper(3))
def helper(x: Int): Int = x * 2 + 1
```

`03-definitions/top-level-val.scala`
```scala
// EXPECT: Hi!
val greeting = "Hi"
@main def run(): Unit = println(greeting + "!")
```

`03-definitions/multiple-parameter-lists.scala`
```scala
// EXPECT: 7 7
def add(a: Int)(b: Int): Int = a + b
@main def run(): Unit =
  val add3 = add(3)
  println(add(3)(4).toString + " " + add3(4))
```

`03-definitions/varargs.scala`
```scala
// EXPECT: 0 3 1,2,3 6
def count(xs: Int*): Int = xs.length
def all(xs: Int*) = xs
def sum(xs: Int*): Int = { var t = 0; xs.foreach(x => t += x); t }
def forward(xs: Int*): Int = sum(xs*)
@main def run(): Unit =
  println(count().toString + " " + count(1, 2, 3) + " " + all(1, 2, 3).mkString(",") + " " + forward(1, 2, 3))
```

`03-definitions/parameterless-def.scala`
```scala
// EXPECT: 3
@main def run(): Unit =
  var calls = 0
  def tick = { calls += 1; calls }
  tick
  tick
  println(tick)
```

`03-definitions/end-markers.scala`
```scala
// EXPECT: big 3
def classify(n: Int): String =
  if n > 10 then
    "big"
  else
    "small"
  end if
end classify

@main def run(): Unit =
  var i = 0
  while i < 3 do
    i += 1
  end while
  println(classify(42) + " " + i)
end run
```

`03-definitions/script-mode.scala`
```scala
// EXPECT: 3
// Script mode (Open question Q2): top-level statements run in order.
val a = 1
val b = 2
println(a + b)
```

`04-control/if-else-indent.scala`
```scala
// EXPECT: neg zero pos
def sign(n: Int): String =
  if n < 0 then "neg"
  else if n == 0 then "zero"
  else "pos"
@main def run(): Unit =
  println(sign(-5) + " " + sign(0) + " " + sign(7))
```

`04-control/if-else-braces.scala`
```scala
// EXPECT: neg zero pos
def sign(n: Int): String = {
  if (n < 0) {
    "neg"
  } else if (n == 0) {
    "zero"
  } else {
    "pos"
  }
}
@main def run(): Unit = {
  println(sign(-5) + " " + sign(0) + " " + sign(7))
}
```

`04-control/if-old-style-indent.scala`
```scala
// EXPECT: neg zero pos
def sign(n: Int): String =
  if (n < 0)
    "neg"
  else if (n == 0)
    "zero"
  else
    "pos"
@main def run(): Unit =
  println(sign(-5) + " " + sign(0) + " " + sign(7))
```

`04-control/if-without-else.scala`
```scala
// EXPECT: ()
@main def run(): Unit =
  val r = if false then 1
  println(r)
```

`04-control/while-indent.scala`
```scala
// EXPECT: 55
@main def run(): Unit =
  var i = 1
  var sum = 0
  while i <= 10 do
    sum += i
    i += 1
  println(sum)
```

`04-control/while-braces.scala`
```scala
// EXPECT: 55
@main def run(): Unit = {
  var i = 1
  var sum = 0
  while (i <= 10) {
    sum += i
    i += 1
  }
  println(sum)
}
```

`04-control/while-old-style-indent.scala`
```scala
// EXPECT: 55
@main def run(): Unit =
  var i = 1
  var sum = 0
  while (i <= 10)
    sum += i
    i += 1
  println(sum)
```

`04-control/nested-blocks-indent.scala`
```scala
// EXPECT: 6
@main def run(): Unit =
  val x =
    val a = 1
    val b =
      val c = 2
      c * 2
    a + b + 1
  println(x)
```

`04-control/nested-blocks-braces.scala`
```scala
// EXPECT: 6
@main def run(): Unit = {
  val x = {
    val a = 1
    val b = {
      val c = 2
      c * 2
    }
    a + b + 1
  }
  println(x)
}
```

`04-control/return-from-def.scala`
```scala
// EXPECT: found 3
def firstOver(limit: Int): Int =
  var i = 0
  while true do
    if i * i > limit then return i
    i += 1
  -1
@main def run(): Unit = println("found " + firstOver(5))
```

`04-control/condition-not-boolean.scala`
```scala
// EXPECT-ERROR: ClassCastException
// D4: Scala rejects this at compile time; protoScala reports it when it runs.
@main def run(): Unit =
  if 1 then println("never")
```

`05-functions/lambdas.scala`
```scala
// EXPECT: 10 7 42 3
@main def run(): Unit =
  val double = (x: Int) => x * 2
  val add = (a: Int, b: Int) => a + b
  val answer = () => 42
  val inc: Int => Int = x => x + 1
  println(double(5).toString + " " + add(3, 4) + " " + answer() + " " + inc(2))
```

`05-functions/higher-order.scala`
```scala
// EXPECT: 20 81
def twice(f: Int => Int, x: Int): Int = f(f(x))
def compose(f: Int => Int, g: Int => Int): Int => Int = x => f(g(x))
@main def run(): Unit =
  val sq = (x: Int) => x * x
  println(twice(x => x + 5, 10).toString + " " + compose(sq, x => x + 1)(8))
```

`05-functions/closure-counter-indent.scala`
```scala
// EXPECT: 3
def makeCounter(): () => Int =
  var count = 0
  () =>
    count += 1
    count
@main def run(): Unit =
  val next = makeCounter()
  next()
  next()
  println(next())
```

`05-functions/closure-counter-braces.scala`
```scala
// EXPECT: 3
def makeCounter(): () => Int = {
  var count = 0
  () => { count += 1; count }
}
@main def run(): Unit = {
  val next = makeCounter()
  next()
  next()
  println(next())
}
```

`05-functions/closure-sees-later-assignment.scala`
```scala
// EXPECT: 5
@main def run(): Unit =
  var x = 1
  val get = () => x
  x = 5
  println(get())
```

`05-functions/closures-per-activation.scala`
```scala
// EXPECT: 0 1
// Each loop iteration's `val j` is a fresh binding (DESIGN §3.5).
@main def run(): Unit =
  var f0: () => Int = null
  var f1: () => Int = null
  var i = 0
  while i < 2 do
    val j = i
    val f = () => j
    if i == 0 then f0 = f else f1 = f
    i += 1
  println(f0().toString + " " + f1())
```

`05-functions/function-apply-method.scala`
```scala
// EXPECT: 49
@main def run(): Unit =
  val sq = (x: Int) => x * x
  println(sq.apply(7))
```

`05-functions/eta-expansion.scala`
```scala
// EXPECT: 9
def square(x: Int): Int = x * x
def applyTo(f: Int => Int, v: Int): Int = f(v)
@main def run(): Unit = println(applyTo(square, 3))
```

`05-functions/local-def-recursion-indent.scala`
```scala
// EXPECT: 120
def fact(n: Int): Int =
  def loop(i: Int, acc: Int): Int =
    if i > n then acc else loop(i + 1, acc * i)
  loop(1, 1)
@main def run(): Unit = println(fact(5))
```

`05-functions/local-def-recursion-braces.scala`
```scala
// EXPECT: 120
def fact(n: Int): Int = {
  def loop(i: Int, acc: Int): Int = {
    if (i > n) acc else loop(i + 1, acc * i)
  }
  loop(1, 1)
}
@main def run(): Unit = { println(fact(5)) }
```

`05-functions/foreach-varargs.scala`
```scala
// EXPECT: sum=10
def sum(xs: Int*): Int =
  var total = 0
  xs.foreach(x => total += x)
  total
@main def run(): Unit = println("sum=" + sum(1, 2, 3, 4))
```

`05-functions/arity-mismatch.scala`
```scala
// EXPECT-ERROR: wrong number of arguments
// D4: a compile-time error in Scala, a runtime error here.
@main def run(): Unit =
  val add = (a: Int, b: Int) => a + b
  println(add(1))
```

`06-recursion/fib-indent.scala`
```scala
// EXPECT: 6765
def fib(n: Int): Int =
  if n < 2 then n
  else fib(n - 1) + fib(n - 2)
@main def run(): Unit = println(fib(20))
```

`06-recursion/fib-braces.scala`
```scala
// EXPECT: 6765
def fib(n: Int): Int = {
  if (n < 2) n
  else fib(n - 1) + fib(n - 2)
}
@main def run(): Unit = { println(fib(20)) }
```

`06-recursion/factorial-bigint.scala`
```scala
// EXPECT: 265252859812191058636308480000000
def fact(n: Int): BigInt = if n <= 1 then 1 else n * fact(n - 1)
@main def run(): Unit = println(fact(30))
```

`06-recursion/mutual-recursion.scala`
```scala
// EXPECT: true
def isEven(n: Int): Boolean = if n == 0 then true else isOdd(n - 1)
def isOdd(n: Int): Boolean = if n == 0 then false else isEven(n - 1)
@main def run(): Unit = println(isEven(10000))
```

`06-recursion/deep-recursion-ok.scala`
```scala
// EXPECT: 50005000
def sumTo(n: Int): Int = if n == 0 then 0 else n + sumTo(n - 1)
@main def run(): Unit = println(sumTo(10000))
```

`06-recursion/stack-overflow.scala`
```scala
// EXPECT-ERROR: StackOverflowError
def down(n: Int): Int = down(n + 1) + 1
@main def run(): Unit = println(down(0))
```

CLI tests (each is `chmod +x`; temporaries in the CTest working directory):

`tests/cli/script-errors.sh`
```bash
#!/usr/bin/env bash
#
# CLI check: failing scripts exit 1 with a located message on stderr, and the
# stdout printed before a runtime error is kept.
#
# Usage: script-errors.sh <path-to-protoscala>
set -u
P="${1:?usage: script-errors.sh <protoscala>}"
work=$(mktemp -d -p "$PWD" script-errors.XXXXXX)
trap 'rm -rf "$work"' EXIT
fail() { echo "FAIL: $*"; exit 1; }

"$P" "$work/missing.scala" >/dev/null 2>"$work/err"; rc=$?
[[ $rc -eq 1 ]] || fail "missing file exited $rc"
grep -q "cannot open" "$work/err" || fail "missing file message: $(cat "$work/err")"

printf 'val x = (1 +\nval y = 2\n' >"$work/parse.scala"
"$P" "$work/parse.scala" >/dev/null 2>"$work/err"; rc=$?
[[ $rc -eq 1 ]] || fail "parse error exited $rc"
grep -qE 'parse\.scala:[0-9]+:[0-9]+: error: ' "$work/err" || fail "parse error format: $(cat "$work/err")"

printf '@main def run(): Unit =\n  println("before")\n  val zero = 0\n  println(1 / zero)\n' >"$work/runtime.scala"
out=$("$P" "$work/runtime.scala" 2>"$work/err"); rc=$?
[[ $rc -eq 1 ]] || fail "runtime error exited $rc"
[[ "$out" == "before" ]] || fail "stdout before the error was lost: '$out'"
grep -q 'runtime\.scala:4: error: ArithmeticException: / by zero' "$work/err" \
    || fail "runtime error format: $(cat "$work/err")"
echo OK
```

`tests/cli/main-args.sh`
```bash
#!/usr/bin/env bash
#
# CLI check: arguments after the script path reach `@main def run(args: String*)`.
#
# Usage: main-args.sh <path-to-protoscala>
set -u
P="${1:?usage: main-args.sh <protoscala>}"
work=$(mktemp -d -p "$PWD" main-args.XXXXXX)
trap 'rm -rf "$work"' EXIT
printf '@main def run(args: String*): Unit =\n  println(args.length.toString + ":" + args.mkString("+"))\n' >"$work/args.scala"
out=$("$P" "$work/args.scala" a b c 2>&1); rc=$?
if [[ $rc -ne 0 || "$out" != "3:a+b+c" ]]; then
    echo "FAIL: exit $rc, output '$out' (expected '3:a+b+c')"
    exit 1
fi
echo OK
```

`tests/cli/disassemble.sh`
```bash
#!/usr/bin/env bash
#
# CLI check: --disassemble prints the bytecode of a script and exits 0.
#
# Usage: disassemble.sh <path-to-protoscala> <tests-source-dir>
set -u
P="${1:?usage: disassemble.sh <protoscala> <tests-dir>}"
T="${2:?usage: disassemble.sh <protoscala> <tests-dir>}"
out=$("$P" --disassemble "$T/conformance/00-binary/hello-world.scala" 2>&1); rc=$?
[[ $rc -eq 0 ]] || { echo "FAIL: exited $rc: $out"; exit 1; }
for piece in "function <top>" "MAKE_FN" "function hello" "CALL 1" "RETURN"; do
    grep -qF -- "$piece" <<<"$out" || { echo "FAIL: no '$piece' in:"; echo "$out"; exit 1; }
done
echo OK
```

`tests/cli/gc-pressure.sh`
```bash
#!/usr/bin/env bash
#
# CLI check: allocating loops and recursion run under a low protoCore heap
# ceiling (DESIGN §10). The program reports the work it did and the check
# verifies it (benchmarks and stress tests self-report).
#
# Usage: gc-pressure.sh <path-to-protoscala> <tests-source-dir>
set -u
P="${1:?usage: gc-pressure.sh <protoscala> <tests-dir>}"
T="${2:?usage: gc-pressure.sh <protoscala> <tests-dir>}"
work=$(mktemp -d -p "$PWD" gc-pressure.XXXXXX)
trap 'rm -rf "$work"' EXIT
cat >"$work/strings.scala" <<'EOF'
@main def run(): Unit =
  var s = ""
  var i = 0
  while i < 20000 do
    s = s + "x"
    i += 1
  var total = 0
  var k = 0
  while k < 20000 do
    total += ("n" + k).length
    k += 1
  println(s.length.toString + " " + total)
EOF
out=$(PROTOCORE_HEAP_LIMIT_CELLS=2000000 timeout 90s "$P" "$work/strings.scala" 2>&1); rc=$?
# "n0".."n9999": 2*10 + 3*90 + 4*900 + 5*9000 + 6*10000 = 108890
[[ $rc -eq 0 && "$out" == "20000 108890" ]] || { echo "FAIL (strings): exit $rc, '$out'"; exit 1; }
out=$(PROTOCORE_HEAP_LIMIT_CELLS=2000000 timeout 90s "$P" "$T/conformance/06-recursion/fib-indent.scala" 2>&1); rc=$?
[[ $rc -eq 0 && "$out" == "6765" ]] || { echo "FAIL (fib): exit $rc, '$out'"; exit 1; }
echo OK
```

(Check of the arithmetic in `gc-pressure.sh`: k = 0..9 → length 2 (10 values), 10..99 → 3
(90), 100..999 → 4 (900), 1000..9999 → 5 (9000), 10000..19999 → 6 (10000):
20 + 270 + 3600 + 45000 + 60000 = 108890.)

`tests/CMakeLists.txt` — replace the CLI loop:

```cmake
foreach(cli_test help version unknown-option script-errors main-args)
    add_test(NAME cli/${cli_test}
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/cli/${cli_test}.sh" "$<TARGET_FILE:protoscala>")
endforeach()
foreach(cli_test disassemble gc-pressure)
    add_test(NAME cli/${cli_test}
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/cli/${cli_test}.sh" "$<TARGET_FILE:protoscala>"
                "${CMAKE_CURRENT_SOURCE_DIR}")
endforeach()
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake -B build_release -S . && cmake --build build_release && ctest --test-dir build_release --output-on-failure`
Expected: the new conformance fixtures and CLI tests FAIL (the binary still prints
"running Scala programs is not implemented yet"); `hello-world.scala` FAILS as EXPECT;
`string-interpolation.scala` passes as XFAIL; unit tests pass.

- [ ] **Step 3: Write `Session.h`**

```cpp
/*
 * Session — one protoScala evaluation session: a ProtoSpace, its Runtime,
 * the globals, the engine and every compiled module (retained for the whole
 * session because function objects point into them; protoClojure
 * src/repl/Repl.cpp retainedModules). Shared by the script runner and the
 * REPL. Construct it on the evaluator thread (StackGuard.h). Phase 1 starts
 * no threads; a later phase that does must join them in ~Session before the
 * ProtoSpace member is destroyed.
 */
#pragma once
#include "compiler/BytecodeModule.h"
#include "compiler/GlobalTable.h"
#include "runtime/ExecutionEngine.h"
#include "runtime/Runtime.h"
#include "protoCore.h"

#include <memory>
#include <string>
#include <vector>

namespace protoScala {

enum class EvalStatus { Ok, Incomplete, Error };

struct EvalOutcome {
    EvalStatus status = EvalStatus::Ok;
    std::vector<std::string> echo;  // REPL lines to print on stdout
};

class Session {
public:
    Session();
    ~Session();

    // Runs a script file; returns the process exit code (0 or 1).
    int runScript(const std::string& path, const std::vector<std::string>& args);
    // Evaluates one REPL input. Incomplete: the input ended inside a construct.
    EvalOutcome evalReplInput(const std::string& source);
    // :load — runs a file in this session (its definitions stay visible).
    bool loadFile(const std::string& path);
    // --disassemble: prints the compiled bytecode of a file; exit code.
    int disassemble(const std::string& path);

private:
    proto::ProtoSpace space_;  // first member: destroyed last
    Runtime runtime_;
    ExecutionEngine engine_;
    GlobalTable globals_;
    std::vector<std::unique_ptr<BytecodeModule>> modules_;
    int resultCounter_ = 0;

    // Parses, compiles and runs one unit. Reports errors on stderr.
    EvalStatus evaluate(const std::string& source, const std::string& sourceName,
                        UnitMode mode, const std::vector<std::string>* mainArgs,
                        EvalOutcome* outcome);
    void callMain(proto::ProtoContext* ctx, const std::string& name, bool takesArgs,
                  const std::vector<std::string>& args);
};

} // namespace protoScala
```

(`Session.h` includes `compiler/Compiler.h` for `UnitMode`.)

- [ ] **Step 4: Write `Session.cpp`**

```cpp
#include "repl/Session.h"
#include "compiler/Compiler.h"
#include "frontend/Desugar.h"
#include "frontend/Parser.h"
#include "runtime/Errors.h"
#include "runtime/Primitives.h"
#include "runtime/Values.h"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace protoScala {

namespace {

bool readFile(const std::string& path, std::string* out) {
    std::ifstream f(path);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    *out = ss.str();
    return true;
}

void reportAt(const std::string& name, SourcePos pos, const std::string& msg) {
    std::fflush(stdout);
    std::fprintf(stderr, "%s:%d:%d: error: %s\n", name.c_str(), pos.line, pos.column, msg.c_str());
}

} // namespace

Session::Session() : runtime_(space_), engine_(runtime_.layout()) {
    installPrimitives(runtime_.rootContext(), runtime_.layout());
    for (const auto& n : builtinGlobalNames()) globals_.declare(n, BindingKind::Builtin);
}

Session::~Session() { std::fflush(stdout); }

EvalStatus Session::evaluate(const std::string& source, const std::string& sourceName,
                             UnitMode mode, const std::vector<std::string>* mainArgs,
                             EvalOutcome* outcome) {
    std::unique_ptr<CompilationUnit> unit;
    try {
        unit = parseSource(source);
    } catch (const ParseError& e) {
        if (mode == UnitMode::Repl && e.atEof) return EvalStatus::Incomplete;
        reportAt(sourceName, e.pos, e.what());
        return EvalStatus::Error;
    }
    GlobalTable trial = globals_;  // committed only if compilation succeeds
    CompiledUnit cu;
    try {
        desugar(*unit);
        Compiler compiler(trial);
        cu = compiler.compileUnit(*unit, mode, resultCounter_);
    } catch (const CompileError& e) {
        reportAt(sourceName, e.pos, e.what());
        return EvalStatus::Error;
    } catch (const std::length_error& e) {  // bytecode limits
        reportAt(sourceName, SourcePos{}, e.what());
        return EvalStatus::Error;
    }
    globals_ = std::move(trial);
    if (!cu.resultName.empty()) ++resultCounter_;

    proto::ProtoContext ctx(&space_, runtime_.rootContext());
    cu.module->linkSymbols(&ctx);
    const BytecodeModule& mod = *cu.module;
    modules_.push_back(std::move(cu.module));
    try {
        engine_.run(&ctx, mod);
        if (!cu.mainName.empty() && mainArgs)
            callMain(&ctx, cu.mainName, cu.mainTakesArgs, *mainArgs);
    } catch (const ScalaError& e) {
        std::fflush(stdout);
        std::fprintf(stderr, "%s:%d: error: %s\n", sourceName.c_str(), e.line, e.what());
        return EvalStatus::Error;
    }
    if (outcome) {
        const RuntimeLayout& L = runtime_.layout();
        auto valueOf = [&](const std::string& name) {
            const auto* key = proto::ProtoString::createSymbol(&ctx, name.c_str());
            const proto::ProtoObject* v = L.globals->getOwnAttributeDirect(&ctx, key);
            return show(&ctx, L, v ? v : PROTO_NONE);
        };
        for (const std::string& d : cu.definitions) {
            if (d.rfind("val ", 0) == 0 || d.rfind("var ", 0) == 0)
                outcome->echo.push_back(d + " = " + valueOf(d.substr(4)));
            else
                outcome->echo.push_back(d);  // "def f", "lazy val x"
        }
        if (!cu.resultName.empty()) {
            const std::string shown = valueOf(cu.resultName);
            if (shown != "()") outcome->echo.push_back("val " + cu.resultName + " = " + shown);
        }
    }
    return EvalStatus::Ok;
}

void Session::callMain(proto::ProtoContext* ctx, const std::string& name, bool takesArgs,
                       const std::vector<std::string>& args) {
    const auto* key = proto::ProtoString::createSymbol(ctx, name.c_str());
    const unsigned n = takesArgs ? static_cast<unsigned>(args.size()) : 0;
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(n + 1);
    scope.setAutomaticLocal(n, runtime_.layout().globals->getOwnAttributeDirect(&scope, key));
    for (unsigned k = 0; k < n; ++k)
        scope.setAutomaticLocal(k, scope.fromUTF8String(args[k].c_str()));
    engine_.callTopLevel(&scope, scope.getAutomaticLocal(n), scope.getAutomaticLocals(), n);
}
```

`callMain` runs outside `ExecutionEngine::run`, so it cannot use `invoke` directly (the
primitives @main reaches read the `ActiveCallContext`). Add this public member to
`ExecutionEngine.h` in this task:

```cpp
    // Like invoke, but installs this engine's ActiveCallContext (entry from
    // C++ code that is not itself running inside the VM, e.g. the @main call).
    const proto::ProtoObject* callTopLevel(proto::ProtoContext* ctx,
                                           const proto::ProtoObject* callable,
                                           const proto::ProtoObject* const* args, unsigned argc);
```

implemented as `{ ActiveGuard guard(this, &layout_); return invoke(ctx, callable, args, argc); }`.

The rest of `Session.cpp`:

```cpp
int Session::runScript(const std::string& path, const std::vector<std::string>& args) {
    std::string source;
    if (!readFile(path, &source)) {
        std::fprintf(stderr, "protoscala: cannot open '%s'\n", path.c_str());
        return 1;
    }
    const EvalStatus s = evaluate(source, path, UnitMode::Script, &args, nullptr);
    std::fflush(stdout);
    return s == EvalStatus::Ok ? 0 : 1;
}

EvalOutcome Session::evalReplInput(const std::string& source) {
    EvalOutcome out;
    out.status = evaluate(source, "<console>", UnitMode::Repl, nullptr, &out);
    return out;
}

bool Session::loadFile(const std::string& path) {
    std::string source;
    if (!readFile(path, &source)) {
        std::fprintf(stderr, "cannot open '%s'\n", path.c_str());
        return false;
    }
    static const std::vector<std::string> noArgs;
    return evaluate(source, path, UnitMode::Script, &noArgs, nullptr) == EvalStatus::Ok;
}

int Session::disassemble(const std::string& path) {
    std::string source;
    if (!readFile(path, &source)) {
        std::fprintf(stderr, "protoscala: cannot open '%s'\n", path.c_str());
        return 1;
    }
    try {
        auto unit = parseSource(source);
        desugar(*unit);
        GlobalTable trial = globals_;
        Compiler compiler(trial);
        const CompiledUnit cu = compiler.compileUnit(*unit, UnitMode::Script, 0);
        std::fputs(cu.module->disassemble().c_str(), stdout);
        return 0;
    } catch (const ParseError& e) {
        reportAt(path, e.pos, e.what());
    } catch (const CompileError& e) {
        reportAt(path, e.pos, e.what());
    }
    return 1;
}

} // namespace protoScala
```

- [ ] **Step 5: Update `src/main.cpp` and CMake**

```cpp
/*
 * protoscala — command-line entry point: runs a Scala 3 script or starts the
 * REPL. Evaluation runs on a dedicated large-stack thread so deep recursion
 * raises StackOverflowError instead of crashing (StackGuard.h).
 */
#include "protoScala/Version.h"
#include "repl/Session.h"
#include "runtime/StackGuard.h"

#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

namespace {

void printVersion() { std::printf("protoScala %s\n", protoScala::versionString()); }

void printHelp() {
    std::printf(
        "Usage: protoscala [options] [script.scala [args...]]\n"
        "\n"
        "Options:\n"
        "  --version            Print version and exit.\n"
        "  --help, -h           Print this help and exit.\n"
        "  --disassemble FILE   Print the compiled bytecode of FILE and exit.\n"
        "\n"
        "With a script: runs its top-level statements, then its @main method\n"
        "(arguments after the script are passed to @main).\n"
        "Without arguments: starts the interactive REPL.\n");
}

struct ScriptJob {
    std::string path;
    std::vector<std::string> args;
    bool disassemble = false;
};

int runJob(void* p) {
    auto* job = static_cast<ScriptJob*>(p);
    protoScala::Session session;
    return job->disassemble ? session.disassemble(job->path)
                            : session.runScript(job->path, job->args);
}

} // namespace

int main(int argc, char** argv) {
    protoScala::configureThreadStacks();
    try {
        if (argc >= 2) {
            const std::string a = argv[1];
            if (a == "--version") { printVersion(); return 0; }
            if (a == "--help" || a == "-h") { printHelp(); return 0; }
            ScriptJob job;
            if (a == "--disassemble") {
                if (argc != 3) {
                    std::fprintf(stderr, "protoscala: --disassemble takes one file\n");
                    return 2;
                }
                job.path = argv[2];
                job.disassemble = true;
                return protoScala::runOnEvaluatorThread(&runJob, &job);
            }
            if (a[0] == '-') {
                std::fprintf(stderr, "protoscala: unknown option '%s'\n", argv[1]);
                printHelp();
                return 2;
            }
            job.path = a;
            job.args.assign(argv + 2, argv + argc);
            return protoScala::runOnEvaluatorThread(&runJob, &job);
        }
        // No arguments: the REPL arrives in Task 11; until then print usage.
        printHelp();
        return 2;
    } catch (const std::exception& e) {
        std::fflush(stdout);
        std::fprintf(stderr, "protoscala: internal error: %s\n", e.what());
        return 1;
    }
}
```

`CMakeLists.txt`:

```cmake
# --- REPL and session (script runner + interactive loop share Session) -----------
add_library(protoscala_repl STATIC
    src/repl/Session.cpp
)
target_link_libraries(protoscala_repl PUBLIC protoscala_runtime protoscala_compiler
    protoscala_frontend protoscala_support)
target_compile_options(protoscala_repl PRIVATE -Wall -Wextra -Wpedantic)
```

and `target_link_libraries(protoscala PRIVATE protoscala_repl)`.

- [ ] **Step 6: Run everything**

Run: `cmake -B build_release -S . && cmake --build build_release && ctest --test-dir build_release --output-on-failure`
Expected: 100 % pass: every fixture above (the interpolation fixture as XFAIL), the new CLI
tests, all unit tests. For any failing fixture, run it by hand
(`build_release/protoscala tests/conformance/<dir>/<file>.scala`) and
`build_release/protoscala --disassemble <file>` before changing code (lesson: disassemble
first).

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt src/main.cpp src/repl src/runtime/ExecutionEngine.h \
        src/runtime/ExecutionEngine.cpp tests
git commit -m "protoscala runs Scala scripts: session, @main, conformance suite

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```


---

### Task 11: The REPL

**Files:**
- Create: `src/repl/Repl.h`, `src/repl/Repl.cpp`
- Create: `tests/cli/repl-basic.sh`, `tests/cli/repl-multiline.sh`, `tests/cli/repl-errors.sh`, `tests/cli/repl-load.sh`, `tests/cli/repl-history-hygiene.sh`
- Modify: `src/repl/Session.h`, `src/repl/Session.cpp` (`evalReplInput` gains `forceComplete`), `src/main.cpp` (no arguments → REPL), `CMakeLists.txt` (readline), `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Session`, `EvalOutcome`, `EvalStatus` (Task 10), `runOnEvaluatorThread` (Task 8).
- Produces: `int runRepl();` (in `protoScala`); `EvalOutcome Session::evalReplInput(const std::string& source, bool forceComplete = false);` — with `forceComplete`, an incomplete input is reported as an error instead of returning `Incomplete`.

REPL surface (Open question Q10 for the echo format): banner
`protoScala <version> REPL — :help for commands, :quit or Ctrl-D to exit`; primary prompt
`scala> `, continuation prompt `     | `; an input is evaluated as soon as it parses
completely (`Incomplete` keeps reading); an empty line at the continuation prompt forces
evaluation (reporting the parse error); commands at the primary prompt: `:help`,
`:quit`/`:q`, `:load <file>`; history in `~/.protoscala_history`, read and written **only
when stdin is a terminal** (so piped sessions and tests never touch `$HOME`); errors are
printed and the loop continues; Ctrl-D exits 0. Without a terminal, prompts are still
printed (no newline), as in protoClojure, so a transcript shows `scala> ` before each echo.

- [ ] **Step 1: Write the failing CLI tests**

`tests/cli/repl-basic.sh`
```bash
#!/usr/bin/env bash
#
# CLI check: the REPL evaluates definitions and expressions from piped stdin,
# echoes values, and exits 0 on :quit.
#
# Usage: repl-basic.sh <path-to-protoscala>
set -u
P="${1:?usage: repl-basic.sh <protoscala>}"
out=$(printf '%s\n' 'val x = 40' 'def inc(n: Int) = n + 1' 'inc(x) + 1' '"a" + "b"' ':quit' \
      | timeout 60s "$P" 2>&1)
rc=$?
[[ $rc -eq 0 ]] || { echo "FAIL: exit $rc"; echo "$out"; exit 1; }
for piece in "protoScala " "val x = 40" "def inc" "val res0 = 42" "val res1 = ab"; do
    grep -qF -- "$piece" <<<"$out" || { echo "FAIL: no '$piece' in:"; echo "$out"; exit 1; }
done
echo OK
```

`tests/cli/repl-multiline.sh`
```bash
#!/usr/bin/env bash
#
# CLI check: incomplete inputs continue on the next line (indentation and
# braces), and an empty continuation line forces evaluation.
#
# Usage: repl-multiline.sh <path-to-protoscala>
set -u
P="${1:?usage: repl-multiline.sh <protoscala>}"
out=$(printf '%s\n' \
    'def twice(n: Int): Int =' \
    '  n * 2' \
    'twice(21)' \
    'val y = {' \
    '  val a = 1' \
    '  a + 1' \
    '}' \
    'val broken = (1 +' \
    '' \
    'y + 1' | timeout 60s "$P" 2>&1)
rc=$?
[[ $rc -eq 0 ]] || { echo "FAIL: exit $rc"; echo "$out"; exit 1; }
for piece in "     | " "def twice" "val res0 = 42" "val y = 2" "<console>:" "val res1 = 3"; do
    grep -qF -- "$piece" <<<"$out" || { echo "FAIL: no '$piece' in:"; echo "$out"; exit 1; }
done
echo OK
```

`tests/cli/repl-errors.sh`
```bash
#!/usr/bin/env bash
#
# CLI check: compile errors, runtime errors and StackOverflowError are
# reported on stderr and the session continues (protoClojure
# tests/cli/repl-stack-overflow.sh is the model).
#
# Usage: repl-errors.sh <path-to-protoscala>
set -u
P="${1:?usage: repl-errors.sh <protoscala>}"
work=$(mktemp -d -p "$PWD" repl-errors.XXXXXX)
trap 'rm -rf "$work"' EXIT
out=$(printf '%s\n' \
    'println(nope)' \
    '1 / 0' \
    'def forever(n: Int): Int = forever(n + 1) + 1' \
    'forever(0)' \
    '40 + 2' | timeout 90s "$P" 2>"$work/err")
rc=$?
[[ $rc -eq 0 ]] || { echo "FAIL: exit $rc"; cat "$work/err"; exit 1; }
grep -q "Not found: nope" "$work/err" || { echo "FAIL: no compile error"; cat "$work/err"; exit 1; }
grep -q "ArithmeticException: / by zero" "$work/err" || { echo "FAIL: no runtime error"; cat "$work/err"; exit 1; }
grep -q "StackOverflowError" "$work/err" || { echo "FAIL: no StackOverflowError"; cat "$work/err"; exit 1; }
grep -qF "val res" <<<"$out" && grep -qF "= 42" <<<"$out" \
    || { echo "FAIL: the session did not continue"; echo "$out"; exit 1; }
echo OK
```

`tests/cli/repl-load.sh`
```bash
#!/usr/bin/env bash
#
# CLI check: :load runs a file in the session; its definitions stay visible.
#
# Usage: repl-load.sh <path-to-protoscala>
set -u
P="${1:?usage: repl-load.sh <protoscala>}"
work=$(mktemp -d -p "$PWD" repl-load.XXXXXX)
trap 'rm -rf "$work"' EXIT
printf 'def square(x: Int): Int = x * x\nval loaded = "yes"\n' >"$work/lib.scala"
out=$(printf '%s\n' ":load $work/lib.scala" 'square(12)' 'loaded' ':help' ':nope' \
      | timeout 60s "$P" 2>&1)
rc=$?
[[ $rc -eq 0 ]] || { echo "FAIL: exit $rc"; echo "$out"; exit 1; }
for piece in "val res0 = 144" "val res1 = yes" ":load <file>" "unknown command: :nope"; do
    grep -qF -- "$piece" <<<"$out" || { echo "FAIL: no '$piece' in:"; echo "$out"; exit 1; }
done
echo OK
```

`tests/cli/repl-history-hygiene.sh`
```bash
#!/usr/bin/env bash
#
# CLI check: a REPL whose stdin is not a terminal never reads or writes the
# history file (tests must not touch $HOME).
#
# Usage: repl-history-hygiene.sh <path-to-protoscala>
set -u
P="${1:?usage: repl-history-hygiene.sh <protoscala>}"
work=$(mktemp -d -p "$PWD" repl-history.XXXXXX)
trap 'rm -rf "$work"' EXIT
mkdir "$work/home"
printf '1 + 1\n:quit\n' | HOME="$work/home" timeout 60s "$P" >/dev/null 2>&1
if [[ -e "$work/home/.protoscala_history" ]]; then
    echo "FAIL: a non-interactive session wrote the history file"
    exit 1
fi
echo OK
```

Add `repl-basic repl-multiline repl-errors repl-load repl-history-hygiene` to the first
`foreach(cli_test ...)` list in `tests/CMakeLists.txt`.

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build_release && ctest --test-dir build_release -R cli/repl --output-on-failure`
Expected: FAIL — without arguments the binary prints usage and exits 2.

- [ ] **Step 3: Implement**

`CMakeLists.txt` — readline (same check as protoClojure `CMakeLists.txt:89-107`):

```cmake
find_library(READLINE_LIBRARY NAMES readline)
find_path(READLINE_INCLUDE_DIR NAMES readline/readline.h)
if(NOT READLINE_LIBRARY OR NOT READLINE_INCLUDE_DIR)
    message(FATAL_ERROR
        "libreadline not found. Install it (Debian/Ubuntu: libreadline-dev; "
        "Fedora/RHEL: readline-devel; macOS: brew install readline).")
endif()
```

and in `protoscala_repl`: add `src/repl/Repl.cpp`,
`target_include_directories(protoscala_repl PRIVATE "${READLINE_INCLUDE_DIR}")`,
`target_link_libraries(protoscala_repl PRIVATE "${READLINE_LIBRARY}")`.

`Session`: change the public signature to
`EvalOutcome evalReplInput(const std::string& source, bool forceComplete = false);` and give
the private `evaluate` a last parameter `bool allowIncomplete`. `evaluate` returns
`EvalStatus::Incomplete` only when `mode == UnitMode::Repl && e.atEof && allowIncomplete`;
otherwise it reports the parse error. `evalReplInput` passes `allowIncomplete =
!forceComplete`; `runScript` and `loadFile` pass `false`.

`Repl.h`:

```cpp
// protoScala interactive REPL (readline): see the Phase 1 plan, Task 11.
#pragma once

namespace protoScala {

// Runs the REPL on the calling thread (call it on the evaluator thread).
// Returns the process exit code.
int runRepl();

} // namespace protoScala
```

`Repl.cpp`:

```cpp
#include "repl/Repl.h"
#include "protoScala/Version.h"
#include "repl/Session.h"

#include <readline/history.h>
#include <readline/readline.h>
// readline/chardefs.h defines a RETURN macro that would break any later
// `Op::RETURN` (protoClojure src/repl/Repl.cpp:15-22).
#ifdef RETURN
#  undef RETURN
#endif

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

namespace protoScala {

namespace {

std::string trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string historyPath() {
    const char* home = std::getenv("HOME");
    return (home && *home) ? std::string(home) + "/.protoscala_history" : std::string();
}

// One line from readline on a terminal, from stdin otherwise (the prompt is
// still printed so transcripts are readable). Copy of protoClojure
// src/repl/Repl.cpp:104-127.
bool readLine(const char* prompt, bool interactive, std::string& out, bool* eof) {
    *eof = false;
    if (interactive) {
        char* line = ::readline(prompt);
        if (!line) { *eof = true; return false; }
        out.assign(line);
        std::free(line);
        return true;
    }
    std::fputs(prompt, stdout);
    std::fflush(stdout);
    std::string buf;
    int ch;
    bool any = false;
    while ((ch = std::getc(stdin)) != EOF) {
        any = true;
        if (ch == '\n') { out = buf; return true; }
        buf.push_back(static_cast<char>(ch));
    }
    if (any) { out = buf; return true; }
    *eof = true;
    return false;
}

void printHelp() {
    std::puts("Commands (at the primary prompt):");
    std::puts("  :help             Show this help");
    std::puts("  :quit, :q         Exit (also Ctrl-D)");
    std::puts("  :load <file>      Run a file in this session");
    std::puts("An input is evaluated as soon as it is complete. An empty line at the");
    std::puts("continuation prompt forces evaluation.");
}

void printOutcome(const EvalOutcome& o) {
    for (const std::string& line : o.echo) std::puts(line.c_str());
    std::fflush(stdout);
}

} // namespace

int runRepl() {
    const bool interactive = ::isatty(STDIN_FILENO) != 0;
    const std::string histPath = historyPath();
    if (interactive && !histPath.empty()) ::read_history(histPath.c_str());

    std::printf("protoScala %s REPL — :help for commands, :quit or Ctrl-D to exit\n",
                versionString());
    Session session;
    std::string buffer;
    for (;;) {
        std::string line;
        bool eof = false;
        const bool continuing = !buffer.empty();
        if (!readLine(continuing ? "     | " : "scala> ", interactive, line, &eof)) {
            if (eof) { std::puts(""); break; }
            continue;
        }
        const std::string trimmed = trim(line);
        if (!continuing && !trimmed.empty() && trimmed[0] == ':') {
            if (interactive) ::add_history(line.c_str());
            if (trimmed == ":quit" || trimmed == ":q") break;
            if (trimmed == ":help") { printHelp(); continue; }
            if (trimmed.rfind(":load ", 0) == 0) { session.loadFile(trim(trimmed.substr(6))); continue; }
            std::fprintf(stderr, "unknown command: %s (try :help)\n", trimmed.c_str());
            continue;
        }
        if (interactive && !trimmed.empty()) ::add_history(line.c_str());
        if (!continuing && trimmed.empty()) continue;
        const bool force = continuing && trimmed.empty();
        buffer += buffer.empty() ? line : "\n" + line;
        const EvalOutcome o = session.evalReplInput(buffer, force);
        if (o.status == EvalStatus::Incomplete) continue;
        printOutcome(o);
        buffer.clear();
    }
    if (interactive && !histPath.empty()) ::write_history(histPath.c_str());
    std::fflush(stdout);
    return 0;
}

} // namespace protoScala
```

(`:help` output lines and `unknown command` go to stdout and stderr respectively;
`repl-load.sh` captures both with `2>&1`.)

`src/main.cpp`: replace the "no arguments" branch with

```cpp
        return protoScala::runOnEvaluatorThread([](void*) { return protoScala::runRepl(); },
                                                nullptr);
```

and include `repl/Repl.h`.

- [ ] **Step 4: Run to verify pass**

Run: `cmake --build build_release && ctest --test-dir build_release --output-on-failure`
Expected: 100 % pass, including `cli/repl-*` and the existing `cli/help` (the help text has
no phase labels).

- [ ] **Step 5: Manual check on a terminal**

Run `build_release/protoscala`, type `val x = 1`, `x + 1`, press ↑ (history recall),
`def f(n: Int) =` Enter `  n * 2` Enter, `f(4)`, Ctrl-D. Expected: echoes `val x = 1`,
`val res0 = 2`, the continuation prompt, `def f`, `val res1 = 8`; after exiting,
`~/.protoscala_history` exists. (This is the maintainer's own terminal session; the
automated tests never create that file.)

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/main.cpp src/repl tests
git commit -m "repl: readline REPL with history, continuation and :load

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```


---

### Task 12: Dual-audience tutorial for the Phase 1 surface

**Files:**
- Create: `docs/TUTORIAL.md`, `docs/tutorial/01-introduction.md`, `docs/tutorial/02-for-the-python-or-javascript-developer.md`, `docs/tutorial/03-for-the-scala-developer.md`, `docs/tutorial/04-values-and-expressions.md`, `docs/tutorial/05-functions-and-closures.md`, `docs/tutorial/14-repl-and-tooling.md`
- Create: fixtures `tests/conformance/tutorial/*.scala` (listed per chapter below)
- Create: `tests/cli/tutorial-repl.sh`; Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: the finished Phase 1 binary (Tasks 1–11); deviation ids of `docs/LANGUAGE.md` §5 and the provisional ids that Task 13 records (the chapter refers to them as "provisional, see STATUS.md").
- Produces: tutorial chapters whose every runnable snippet is a conformance fixture named `tests/conformance/tutorial/NN-<chapter>-<name>.scala` (NN = chapter number), and whose REPL transcript is replayed by `tests/cli/tutorial-repl.sh`, so the tutorial cannot drift from the implementation.

Structure and tone follow protoClojure's tutorial (`docs/TUTORIAL.md`, `docs/tutorial/02-for-the-python-or-javascript-developer.md`, `docs/tutorial/03-for-the-clojure-developer.md`): an index with a "What runs today" note; chapter 2 teaches from first principles with constant bridges to Python/JavaScript; chapter 3 is an honest catalogue of departures keyed to `D<n>` ids; every chapter that mentions not-yet-implemented features opens with an "Implementation status" note. Rule for authors: **a snippet appears in a chapter only if it is (a) the body of a fixture listed here, verbatim, or (b) a line of the replayed REPL transcript.** The printed output shown in the chapter is the fixture's `EXPECT` line.

- [ ] **Step 1: Write the fixtures (they fail only if the implementation disagrees)**

Chapter 1 — `01-introduction`:

`tutorial/01-introduction-hello.scala`
```scala
// EXPECT: Hello, protoScala!
@main def hello(): Unit =
  println("Hello, protoScala!")
```

`tutorial/01-introduction-first-program.scala`
```scala
// EXPECT: The answer is 42
def answer(): Int = 6 * 7
@main def run(): Unit =
  println("The answer is " + answer())
```

Chapter 2 — `02-python-js`:

`tutorial/02-python-js-val-var.scala`
```scala
// EXPECT: 3 visits, name=Ada
@main def run(): Unit =
  val name = "Ada"      // like a JS const, or a Python name you never rebind
  var visits = 0        // like a JS let
  visits += 1
  visits += 2
  println(visits.toString + " visits, name=" + name)
```

`tutorial/02-python-js-if-expression.scala`
```scala
// EXPECT: minor adult
def category(age: Int): String = if age < 18 then "minor" else "adult"
@main def run(): Unit =
  println(category(12) + " " + category(40))
```

`tutorial/02-python-js-blocks-are-expressions.scala`
```scala
// EXPECT: 25
@main def run(): Unit =
  val area =
    val width = 5
    val height = 5
    width * height
  println(area)
```

`tutorial/02-python-js-indentation-and-braces.scala`
```scala
// EXPECT: 6 6
def sumIndented(n: Int): Int =
  var total = 0
  var i = 1
  while i <= n do
    total += i
    i += 1
  total

def sumBraces(n: Int): Int = {
  var total = 0
  var i = 1
  while (i <= n) {
    total += i
    i += 1
  }
  total
}

@main def run(): Unit =
  println(sumIndented(3).toString + " " + sumBraces(3))
```

`tutorial/02-python-js-arrow-functions.scala`
```scala
// EXPECT: 14 12
@main def run(): Unit =
  val double = (x: Int) => x * 2          // JS: x => x * 2, Python: lambda x: x * 2
  def applyTwice(f: Int => Int, x: Int): Int = f(f(x))
  println(double(7).toString + " " + applyTwice(double, 3))
```

`tutorial/02-python-js-closure-counter.scala`
```scala
// EXPECT: 1 2 3
def makeCounter(): () => Int =
  var count = 0
  () =>
    count += 1
    count
@main def run(): Unit =
  val next = makeCounter()
  val a = next()
  val b = next()
  println(a.toString + " " + b + " " + next())
```

`tutorial/02-python-js-big-integers.scala`
```scala
// EXPECT: 1267650600228229401496703205376
// Like Python, integers never overflow (D1). This is 2 to the power 100.
@main def run(): Unit =
  var result: BigInt = 1
  var i = 0
  while i < 100 do
    result = result * 2
    i += 1
  println(result)
```

`tutorial/02-python-js-type-annotations.scala`
```scala
// EXPECT: 5
// Types are annotations, like TypeScript or Python type hints; they are not checked (D4).
def add(a: Int, b: Int): Int = a + b
@main def run(): Unit =
  val total: Int = add(2, 3)
  println(total)
```

Chapter 3 — `03-scala-dev`:

`tutorial/03-scala-dev-d1-no-overflow.scala`
```scala
// EXPECT: 2147483648 9223372036854775808
// D1: Scala on the JVM prints -2147483648 -9223372036854775808.
@main def run(): Unit =
  val i: Int = 2147483647
  val l: Long = 9223372036854775807L
  println((i + 1).toString + " " + (l + 1))
```

`tutorial/03-scala-dev-d2-float-is-double.scala`
```scala
// EXPECT: 0.30000000000000004
// D2: Float is Double, so this sum is computed in double precision.
@main def run(): Unit =
  println(0.1f + 0.2f)
```

`tutorial/03-scala-dev-d4-runtime-type-error.scala`
```scala
// EXPECT-ERROR: ClassCastException
// D4: no static type checker; scalac rejects this program, protoScala fails when it runs.
@main def run(): Unit =
  val n = 1
  if n then println("never")
```

`tutorial/03-scala-dev-script-mode.scala`
```scala
// EXPECT: script mode works
// Top-level statements run in order, like a .sc script (provisional; see STATUS.md).
val words = "script mode"
println(words + " works")
```

`tutorial/03-scala-dev-eager-top-level-vals.scala`
```scala
// EXPECT: main
// Top-level vals are initialised eagerly, before @main (provisional; see STATUS.md).
// Scala 3 initialises them on first access, so it would never print "init" here.
val unused = { println("init"); 0 }
@main def run(): Unit = println("main")
```

Chapter 4 — `04-values`:

`tutorial/04-values-literals.scala`
```scala
// EXPECT: 255 1000000 2.5 1.0E-4 x true
@main def run(): Unit =
  println(0xFF.toString + " " + 1_000_000 + " " + 2.5 + " " + 0.0001 + " " + 'x' + " " + true)
```

`tutorial/04-values-operators-are-methods.scala`
```scala
// EXPECT: 3 3 5
@main def run(): Unit =
  println((1 + 2).toString + " " + 1.+(2) + " " + (3 max 5))
```

`tutorial/04-values-strings.scala`
```scala
// EXPECT: HELLO 5 ell hello!!! true
@main def run(): Unit =
  val s = "hello"
  println(s.toUpperCase + " " + s.length + " " + s.substring(1, 4) + " " + s + "!" * 3 + " " + s.startsWith("he"))
```

`tutorial/04-values-equality.scala`
```scala
// EXPECT: true true false true
@main def run(): Unit =
  val a = "pro" + "to"
  println((a == "proto").toString + " " + (1 == 1.0) + " " + (1 == 2) + " " + (a != "Scala"))
```

`tutorial/04-values-if-and-while.scala`
```scala
// EXPECT: 1 2 fizz 4 buzz
def label(n: Int): String =
  if n % 3 == 0 then "fizz"
  else if n % 5 == 0 then "buzz"
  else n.toString

@main def run(): Unit =
  var out = ""
  var i = 1
  while i <= 5 do
    out = out + (if i == 1 then "" else " ") + label(i)
    i += 1
  println(out)
```

`tutorial/04-values-unit.scala`
```scala
// EXPECT: ()
@main def run(): Unit =
  val nothing = println("side effect")
  println(nothing)
```

Chapter 5 — `05-functions`:

`tutorial/05-functions-def.scala`
```scala
// EXPECT: 7 hello, Ada
def add(a: Int, b: Int): Int = a + b
def greet(name: String): String = "hello, " + name
@main def run(): Unit =
  println(add(3, 4).toString + " " + greet("Ada"))
```

`tutorial/05-functions-currying.scala`
```scala
// EXPECT: 15 15
def multiply(a: Int)(b: Int): Int = a * b
@main def run(): Unit =
  val triple = multiply(3)
  println(multiply(3)(5).toString + " " + triple(5))
```

`tutorial/05-functions-varargs.scala`
```scala
// EXPECT: 10 0 6
def sum(xs: Int*): Int =
  var total = 0
  xs.foreach(x => total += x)
  total
def sumAll(xs: Int*): Int = sum(xs*)
@main def run(): Unit =
  println(sum(1, 2, 3, 4).toString + " " + sum() + " " + sumAll(1, 2, 3))
```

`tutorial/05-functions-higher-order.scala`
```scala
// EXPECT: 16 10
def compose(f: Int => Int, g: Int => Int): Int => Int = x => f(g(x))
@main def run(): Unit =
  val square = (x: Int) => x * x
  val inc = (x: Int) => x + 1
  println(compose(square, inc)(3).toString + " " + compose(inc, square)(inc(2)))
```

`tutorial/05-functions-closures.scala`
```scala
// EXPECT: 5 2
@main def run(): Unit =
  var x = 1
  val readX = () => x
  x = 5
  var calls = 0
  val record = () => calls += 1
  record()
  record()
  println(readX().toString + " " + calls)
```

`tutorial/05-functions-local-recursion.scala`
```scala
// EXPECT: 3628800 true
def factorial(n: Int): BigInt =
  def loop(i: Int, acc: BigInt): BigInt =
    if i > n then acc else loop(i + 1, acc * i)
  loop(1, 1)

def isEven(n: Int): Boolean =
  def even(k: Int): Boolean = if k == 0 then true else odd(k - 1)
  def odd(k: Int): Boolean = if k == 0 then false else even(k - 1)
  even(n)

@main def run(): Unit =
  println(factorial(10).toString + " " + isEven(1000))
```

`tutorial/05-functions-lazy-val.scala`
```scala
// EXPECT: start computed 42 42
@main def run(): Unit =
  var log = "start"
  lazy val expensive = { log = log + " computed"; 42 }
  val a = expensive
  val b = expensive
  println(log + " " + a + " " + b)
```

`tutorial/05-functions-parameterless.scala`
```scala
// EXPECT: 1 2
@main def run(): Unit =
  var n = 0
  def next = { n += 1; n }
  val first = next
  println(first.toString + " " + next)
```

Chapter 14 — `14-repl`:

`tutorial/14-repl-script-args.scala`
```scala
// EXPECT: args: 0
@main def run(args: String*): Unit =
  println("args: " + args.length)
```

`tests/cli/tutorial-repl.sh` replays the chapter-14 transcript:

```bash
#!/usr/bin/env bash
#
# CLI check: the REPL session printed in docs/tutorial/14-repl-and-tooling.md
# §14.2 produces exactly the echoes the chapter shows.
#
# Usage: tutorial-repl.sh <path-to-protoscala>
set -u
P="${1:?usage: tutorial-repl.sh <protoscala>}"
out=$(printf '%s\n' \
    'val greeting = "Hello"' \
    'def shout(s: String) = s.toUpperCase + "!"' \
    'shout(greeting)' \
    'def fact(n: Int): BigInt = if n <= 1 then 1 else n * fact(n - 1)' \
    'fact(20)' \
    'def sumTo(n: Int): Int = {' \
    '  var total = 0' \
    '  var i = 1' \
    '  while i <= n do { total += i; i += 1 }' \
    '  total' \
    '}' \
    'sumTo(100)' \
    ':quit' | timeout 60s "$P" 2>&1)
rc=$?
[[ $rc -eq 0 ]] || { echo "FAIL: exit $rc"; echo "$out"; exit 1; }
for piece in "val greeting = Hello" "def shout" "val res0 = HELLO!" "def fact" \
             "val res1 = 2432902008176640000" "def sumTo" "val res2 = 5050"; do
    grep -qF -- "$piece" <<<"$out" || { echo "FAIL: no '$piece' in:"; echo "$out"; exit 1; }
done
echo OK
```

Add `tutorial-repl` to the first CLI `foreach` list in `tests/CMakeLists.txt`.

- [ ] **Step 2: Run the fixtures**

Run: `cmake -B build_release -S . && cmake --build build_release && ctest --test-dir build_release -R "tutorial" --output-on-failure`
Expected: all PASS. A failure means either the implementation has a bug (fix it with a
regular fixture first) or the expected line is wrong (re-derive it by hand from Scala 3
semantics; never copy the binary's output into an `EXPECT` without checking it).

- [ ] **Step 3: Write the chapters**

`docs/TUTORIAL.md` (index), modelled on protoClojure's `docs/TUTORIAL.md:1-74`:
- Title "The protoScala Tutorial" and the dual-audience paragraph (Python/JavaScript
  developers learn Scala 3 from first principles; Scala developers get the catalogue of
  departures from Scala 3 on the JVM).
- Links to `LANGUAGE.md` (authoritative reference), `STATUS.md` (what works, deviations),
  `DESIGN.md`.
- **What runs today (protoScala 0.1.0)** note: chapters 1, 2, 3, 4, 5 and 14 describe what
  the binary runs: values and expressions, `val`/`var`/`lazy val`/`def`, `if`/`while`,
  lambdas and closures, recursion, integer and string arithmetic, `println`, the REPL, in
  brace and indentation syntax. Classes, pattern matching, collections, for-comprehensions,
  string interpolation, exceptions, actors and modules are planned; every runnable snippet
  is a conformance fixture under `tests/conformance/tutorial/`.
- "How to use this tutorial" for both audiences (as protoClojure's).
- Chapter table: 1 Introduction; 2 For the Python or JavaScript developer; 3 For the Scala
  developer; 4 Values and expressions; 5 Functions and closures; 6 Classes, objects and
  traits (planned); 7 Case classes and pattern matching (planned); 8 Collections (planned);
  9 For-comprehensions (planned); 10 Strings and interpolation (planned); 11 Exceptions
  (planned); 12 Enums and sealed hierarchies (planned); 13 Actors and futures (planned);
  14 The REPL and tooling. Planned chapters have no link until written.
- "Running the examples": `protoscala file.scala [args...]`, `protoscala` (REPL),
  `protoscala --version`, `protoscala --disassemble file.scala`.

Chapter outlines (each section cites its fixture by path; outputs are the `EXPECT` lines):

`01-introduction.md` — 1.1 What protoScala is (a dynamic Scala 3 dialect on protoCore; not
a JVM, no scalac typechecker; instant start-up, immutable structures, real concurrency and
polyglot interop as the direction — DESIGN §1). 1.2 Building (`cmake -B build_release -S .`
…, protoCore sibling tree). 1.3 Your first program (`01-introduction-hello.scala`; `@main`).
1.4 A program with a definition (`01-introduction-first-program.scala`). 1.5 A first look
at the REPL (points to chapter 14). 1.6 What works today and where to look (STATUS.md).

`02-for-the-python-or-javascript-developer.md` — opens with an Implementation status note
(classes, collections and string interpolation are later chapters). 2.1 Programs and
`@main` (vs `if __name__ == "__main__"` and a top-level script; script mode). 2.2 Names:
`val` vs `var` (vs `const`/`let`, Python names) — `02-python-js-val-var.scala`.
2.3 Everything is an expression: `if` returns a value (vs the ternary / conditional
expression) — `02-python-js-if-expression.scala`; blocks return their last expression —
`02-python-js-blocks-are-expressions.scala`. 2.4 Indentation vs braces: Scala 3 accepts
both; the indentation rules compared with Python's colon-and-indent; `then`/`do`/`end`
markers — `02-python-js-indentation-and-braces.scala`. 2.5 Functions and arrow functions
(`def`, lambdas vs `x => …` and `lambda x: …`, passing functions) —
`02-python-js-arrow-functions.scala`. 2.6 Closures (the counter idiom from JS) —
`02-python-js-closure-counter.scala`. 2.7 Numbers: integers never overflow, like Python and
unlike JavaScript's doubles (D1) — `02-python-js-big-integers.scala`. 2.8 Types as
annotations (like TypeScript / type hints; erased, not checked, D4) —
`02-python-js-type-annotations.scala`. 2.9 Where to go next.

`03-for-the-scala-developer.md` — framing: a dialect, not a port; every divergence has a
`D<n>` id in STATUS.md. 3.1 What is identical (syntax: braces and indentation, `end`
markers, operators as methods, precedence and right-associative `:` operators, curried
defs and Scala 3 eta-expansion, varargs and `xs*`, closures over `var`s, `lazy val`,
`@main`). 3.2 Departures, keyed to LANGUAGE.md §5: D1 (`03-scala-dev-d1-no-overflow.scala`),
D2 (`03-scala-dev-d2-float-is-double.scala`), D3 no implicits/givens (rejected with a
message), D4 no static typing (`03-scala-dev-d4-runtime-type-error.scala`), D5 access
modifiers advisory, D6 extension methods (later), D7 Map/Set order (later), D8 no Java
interop; then the provisional deviations recorded in STATUS.md by this phase (script mode
`03-scala-dev-script-mode.scala`; eager top-level initialisation
`03-scala-dev-eager-top-level-vals.scala`; varargs arrive as `List`; string length and
indices in code points; ASCII case mapping; no non-local `return`; error names without
`java.lang.`; `>>>` unsupported; tabs/spaces mixing rejected; multi-line lambda bodies
inside parentheses need braces), each one paragraph with the Scala behaviour and the
protoScala behaviour. 3.3 What is missing (implicits/givens, the type checker, the JVM
and Java libraries, macros; classes/objects/traits, pattern matching, for-comprehensions,
collections, interpolation, exceptions, actors — with the phase ROADMAP gives each). 3.4
What is new (protoCore: persistent structures, GIL-free concurrency, polyglot UMD — as
direction, pointing to DESIGN).

`04-values-and-expressions.md` — 4.1 Literals (`04-values-literals.scala`). 4.2 Integers
and doubles: promotion, `/` and `%` semantics, Java-style double printing. 4.3 Operators
are methods, precedence by first character, infix method calls
(`04-values-operators-are-methods.scala`). 4.4 Strings and characters
(`04-values-strings.scala`). 4.5 Equality: `==`, `!=`, cooperative numeric equality
(`04-values-equality.scala`). 4.6 `if` and `while`, both syntaxes (`04-values-if-and-while.scala`).
4.7 `Unit` and `()` (`04-values-unit.scala`).

`05-functions-and-closures.md` — 5.1 `def` (`05-functions-def.scala`). 5.2 Multiple parameter
lists and partial application (`05-functions-currying.scala`). 5.3 Varargs and splices
(`05-functions-varargs.scala`). 5.4 Lambdas and higher-order functions
(`05-functions-higher-order.scala`). 5.5 Closures: capture of `var`s and per-iteration
bindings (`05-functions-closures.scala`; the loop case refers to
`tests/conformance/05-functions/closures-per-activation.scala`). 5.6 Local and mutually
recursive functions; recursion depth and `StackOverflowError`
(`05-functions-local-recursion.scala`). 5.7 `lazy val` (`05-functions-lazy-val.scala`).
5.8 Parameterless defs (`05-functions-parameterless.scala`).

`14-repl-and-tooling.md` — 14.1 Starting the REPL; prompts `scala>` and `     |`. 14.2 A
session (the exact transcript of `tests/cli/tutorial-repl.sh`, with the echoes it checks);
the echo format (`val x = …`, `def f`, `val resN = …`; no types — D4). 14.3 Multi-line
input: an input runs as soon as it is complete; a line ending in `=` continues; braces
continue until closed; an empty continuation line forces evaluation; pitfall: `if … then …`
at the end of a line is already complete, so put `else` on the same line or use braces.
14.4 Commands `:help`, `:quit`, `:load <file>`; history in `~/.protoscala_history`
(interactive sessions only). 14.5 Running scripts, arguments to `@main`
(`14-repl-script-args.scala`), exit codes (0, 1 on errors, 2 on bad options). 14.6 Error
messages (`file:line:col: error:` for parse/compile, `file:line: error: Class: message` at
run time). 14.7 `--disassemble` and `--version`.

- [ ] **Step 4: Verify every chapter snippet is a fixture**

Run: `grep -n '^```scala' -A2 docs/tutorial/*.md | head -80` and check by eye that each
scala block is the body of a listed fixture (or a transcript line of 14.2); then
`ctest --test-dir build_release --output-on-failure` → 100 %.

- [ ] **Step 5: Commit**

```bash
git add docs/TUTORIAL.md docs/tutorial tests/conformance/tutorial tests/cli/tutorial-repl.sh tests/CMakeLists.txt
git commit -m "docs: dual-audience tutorial chapters 1-5 and 14 with fixtures

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```


---

### Task 13: Examples, cold-start measurement, status documents, version 0.1.0

**Files:**
- Create: `examples/hello.scala`, `examples/fib.scala`, `tests/cli/examples.sh`, `benchmarks/cold-start.sh`, `benchmarks/RESULTS.md`, `CHANGELOG.md`, `README.md`, `LICENSE`
- Modify: `CMakeLists.txt` (version 0.1.0), `tests/CMakeLists.txt`, `docs/STATUS.md`, `docs/ROADMAP.md`, `docs/LANGUAGE.md`, `docs/TUTORIAL.md` (version in the note, if it changed)

**Interfaces:**
- Consumes: the complete Phase 1 binary.
- Produces: the Phase 1 release state (ROADMAP "Done when" satisfied and documented).

- [ ] **Step 1: Examples and their check (failing first)**

`examples/hello.scala`
```scala
// Run: protoscala examples/hello.scala
@main def hello(): Unit =
  println("Hello, protoScala!")
```

`examples/fib.scala`
```scala
// Recursive Fibonacci. Run: protoscala examples/fib.scala [n]
def fib(n: Int): Int =
  if n < 2 then n
  else fib(n - 1) + fib(n - 2)

@main def run(args: String*): Unit =
  val n = if args.isEmpty then 25 else args(0).toInt
  println("fib(" + n + ") = " + fib(n))
```

`tests/cli/examples.sh`
```bash
#!/usr/bin/env bash
#
# CLI check: the shipped examples run and print what they compute.
#
# Usage: examples.sh <path-to-protoscala> <repo-root>
set -u
P="${1:?usage: examples.sh <protoscala> <repo-root>}"
R="${2:?usage: examples.sh <protoscala> <repo-root>}"
out=$(timeout 60s "$P" "$R/examples/hello.scala" 2>&1); rc=$?
[[ $rc -eq 0 && "$out" == "Hello, protoScala!" ]] || { echo "FAIL hello: exit $rc, '$out'"; exit 1; }
out=$(timeout 60s "$P" "$R/examples/fib.scala" 2>&1); rc=$?
[[ $rc -eq 0 && "$out" == "fib(25) = 75025" ]] || { echo "FAIL fib: exit $rc, '$out'"; exit 1; }
out=$(timeout 60s "$P" "$R/examples/fib.scala" 10 2>&1); rc=$?
[[ $rc -eq 0 && "$out" == "fib(10) = 55" ]] || { echo "FAIL fib 10: exit $rc, '$out'"; exit 1; }
echo OK
```

In `tests/CMakeLists.txt` add:

```cmake
add_test(NAME cli/examples
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/cli/examples.sh" "$<TARGET_FILE:protoscala>"
            "${CMAKE_SOURCE_DIR}")
```

Run: `ctest --test-dir build_release -R cli/examples --output-on-failure` → FAIL before the
examples exist, PASS after creating them.

- [ ] **Step 2: Self-reporting cold-start measurement**

`benchmarks/cold-start.sh`
```bash
#!/usr/bin/env bash
#
# Cold-start measurement (DESIGN §1: < 20 ms to a prompt). Runs
#   (a) examples/hello.scala and
#   (b) a REPL session that quits immediately
# N times each, verifies every run's output, and reports the median wall
# time in milliseconds. Exits 1 when a run's output is wrong or a median is
# not below the target. Not part of ctest: timings depend on the machine
# and its load (see docs/plans Open question Q16).
#
# Usage: benchmarks/cold-start.sh [path-to-protoscala] [runs]
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
P="${1:-$root/build_release/protoscala}"
N="${2:-21}"
TARGET_MS=20

median_ms() {  # reads nanosecond samples on stdin, prints the median in ms
    sort -n | awk '{ a[NR] = $1 } END { printf "%.2f", a[int((NR + 1) / 2)] / 1e6 }'
}

run_case() {  # $1 label, $2 expected last line, $3... command
    local label="$1" expected="$2"; shift 2
    local samples="" ok=0 i
    for ((i = 0; i < N; i++)); do
        local t0 t1 out
        t0=$(date +%s%N)
        out=$("$@" 2>&1 | awk 'NF{ last=$0 } END{ print last }')
        t1=$(date +%s%N)
        [[ "$out" == "$expected" ]] && ok=$((ok + 1))
        samples+="$((t1 - t0))"$'\n'
    done
    local med
    med=$(printf '%s' "$samples" | median_ms)
    echo "$label: runs=$N verified=$ok median_ms=$med target_ms=$TARGET_MS"
    [[ $ok -eq $N ]] || { echo "$label: FAIL: $((N - ok)) run(s) printed the wrong output"; return 1; }
    awk -v m="$med" -v t="$TARGET_MS" 'BEGIN { exit !(m < t) }' \
        || { echo "$label: FAIL: median ${med} ms is not below ${TARGET_MS} ms"; return 1; }
}

status=0
run_case "script" "Hello, protoScala!" "$P" "$root/examples/hello.scala" || status=1
run_case "repl" "scala> " sh -c "printf ':quit\n' | '$P'" || status=1
exit $status
```

(The REPL case's last non-empty output line is the prompt printed before `:quit` is read:
`scala> ` — awk keeps trailing blanks, so the expected text includes the space.)

Run it three times: `benchmarks/cold-start.sh build_release/protoscala 21` (build with
`-DCMAKE_BUILD_TYPE=Release` first if the maintainer wants release numbers; record which).
Create `benchmarks/RESULTS.md`:

```markdown
# protoScala benchmark results

Every benchmark prints the work it did and its runner verifies it; exit code alone never
counts as success (DESIGN §10).

## Cold start (Phase 1)

`benchmarks/cold-start.sh build_release/protoscala 21`, <machine>, <build type>, <date>.

| Case | Runs | Verified | Median (ms) | Target |
|---|---|---|---|---|
| script (`examples/hello.scala`) | 21 | 21 | <measured> | < 20 |
| REPL to prompt and `:quit` | 21 | 21 | <measured> | < 20 |
```

Fill `<machine>` (`uname -m` and CPU model from `lscpu`), build type, date (`date +%F`) and
the measured medians from the script's output — these are measurements, not placeholders.
If a median is not below 20 ms, **stop and report to the maintainer** with a `perf stat -r
3` profile of the script case (lesson: measure before optimising); do not tune the
runtime in this task.

- [ ] **Step 3: Status documents**

`docs/STATUS.md`:
- Current state: "Phase 1 complete (0.1.0): lexer with the Scala 3 offside rule, parser,
  compiler and VM for values, `val`/`var`/`lazy val`/`def`, `if`/`while`, lambdas and
  closures, recursion with `StackOverflowError`, `println`, readline REPL." Test counts from
  `ctest --test-dir build_release -N | tail -1` and the date.
- "Implemented" list per LANGUAGE §1–§2 rows delivered.
- **Opcode table** (DESIGN §3.5 requires the mirror): number, name, stack effect, copied
  from `src/compiler/Opcodes.h`.
- **Deviations:** keep D1–D8; add the provisional ones this phase introduced, each marked
  "provisional — pending maintainer decision (plan Q<n>)":

  | Id | Deviation | Plan question |
  |---|---|---|
  | D9 | Script mode: top-level statements run in order; top-level vals are initialised eagerly before `@main` (Scala 3 initialises a file's top-level definitions on first access) | Q2 |
  | D10 | A bare reference to `def f()` (empty parameter list) evaluates to the function (Scala 3 requires `f()` unless a function type is expected) | Q3 |
  | D11 | `return` inside a lambda (non-local return) is rejected | Q5 |
  | D12 | Varargs arrive as a `List` (`toString` prints `List(...)`, Scala prints `ArraySeq(...)`) | Q15 |
  | D13 | String length and indices count code points, not UTF-16 units; case mapping is ASCII-only | Q11 |
  | D14 | Runtime errors use unqualified class names (`ArithmeticException`, no `java.lang.`) and the format `file:line: error: Class: message`; arity mismatches raise `IllegalArgumentException` | Q9 |
  | D15 | `>>>` raises `UnsupportedOperationException` (integers have no fixed width, D1) | Q12 |
  | D16 | Indentation mixing tabs and spaces is rejected | Q13 |
  | D17 | Inside `(...)`, a multi-statement lambda body needs braces (no indentation region inside parentheses) | Q1 |
  | D18 | A `:` at the end of a line always opens an indentation region (`val x:` + newline + type is rejected) | Q14 |

- Known issues / platform dependencies: R1 (back-edge safepoint now used provisionally,
  Q21), R2, R4, R5, R8 unchanged.

`docs/ROADMAP.md`: Phase 1 heading gets `✅ (<date>)`; its `**Plan:**` link becomes
`[plans/2026-09-22-phase-1-core-language.md](plans/2026-09-22-phase-1-core-language.md)`
(the old link pointed to a file that does not exist).

`docs/LANGUAGE.md` §1: add `this` to the hard-keyword list (it is a Scala 3 hard keyword;
the list omitted it) — a documentation fix, no semantic change.

`CHANGELOG.md` (Keep a Changelog, as protoClojure's `CHANGELOG.md`):

```markdown
# Changelog

All notable changes to protoScala are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [0.1.0] - <date of this commit, date +%F>

### Added

- **Lexer** for Scala 3 lexical syntax: alphanumeric, operator, mixed and backquoted
  identifiers; hard and soft keywords; decimal, hex and binary integers with `_` and `L`;
  floating-point literals; characters and strings with escapes; triple-quoted strings;
  interpolated strings as structured tokens; nested comments.
- **Significant indentation** (Scala 3 offside rule) and braces, mixable; `end` markers.
- **Parser and compiler** for `val`, `var`, `lazy val`, `def` (multiple parameter lists,
  varargs, `@main`), `if`/`then`/`else`, `while`/`do`, blocks, lambdas, closures with
  per-activation captures, `return` in methods, imports (parsed).
- **Bytecode VM** on protoCore with SmallInteger fast paths and arbitrary-precision
  promotion (D1), Java-style double printing, `StackOverflowError` instead of crashes.
- **Standard surface:** `println`, `print`, methods of Int, Double, Boolean, Char, String,
  List (varargs) and functions.
- **REPL** with readline history, multi-line continuation, `:help`, `:quit`, `:load`.
- **Tooling:** `--disassemble`; conformance, unit and CLI test suites; tutorial chapters
  1–5 and 14.
```

`README.md`: title and one-paragraph description (DESIGN §1 vision, "not production
ready"), build instructions (protoCore sibling tree, `cmake -B build_release -S .`, test
command), usage (`protoscala file.scala`, `protoscala` for the REPL), links to
`docs/TUTORIAL.md`, `docs/LANGUAGE.md`, `docs/STATUS.md`, `docs/DESIGN.md`,
`docs/ROADMAP.md`, license line. No phase labels in user-facing usage text.

`LICENSE`: provisional — copy protoClojure's `LICENSE` (MIT, "Copyright (c) 2026 Gustavo
Marino <gamarino@gmail.com>") pending Open question Q17. (`CMakeLists.txt` already installs
`LICENSE` and `README.md`; without them `cmake --install` fails today.)

`CMakeLists.txt`: `project(protoScala VERSION 0.1.0 LANGUAGES CXX)`.

- [ ] **Step 4: Full verification**

Run: `rm -rf build_release && cmake -B build_release -S . && cmake --build build_release && ctest --test-dir build_release --output-on-failure`
(`rm -rf build_release` is inside the repository; check the path before running it.)
Expected: 100 % pass; `build_release/protoscala --version` prints `protoScala 0.1.0`.
Then walk the ROADMAP Phase 1 "Done when" list and tick each item against evidence:
lexer tests (`ctest -R "Lexer|Layout"`), brace/indentation fixtures (`ctest -R conformance`),
`06-recursion/stack-overflow.scala`, `cli/repl-*`, `cli/examples`, the cold-start results
file.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt examples benchmarks CHANGELOG.md README.md LICENSE docs tests
git commit -m "Release 0.1.0: Phase 1 complete (examples, cold start, status)

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

Tagging `v0.1.0` is the maintainer's decision; do not create the tag.

---

## Self-review against DESIGN.md and ROADMAP Phase 1

ROADMAP Phase 1 "Done when":

| Criterion | Where it is satisfied |
|---|---|
| Lexer unit tests cover every token class | Task 1 `test_lexer.cpp`: identifiers (alphanumeric, operator, mixed, backquoted, Unicode), hard and soft keywords, reserved operators, int (dec/hex/bin/`_`/`L`/big), float, char, string, triple-quoted, interpolated, comments (nested), punctuation, positions |
| … and every offside-rule region (DESIGN §3.2), braces and indentation variants | Task 2 `test_layout.cpp`: `RegionOpenersFromDesignList` (every opener of §3.2), `ColonAtEndOfLineOpensTemplateBody` (template bodies, both variants), `if`/`while` both styles, old-style conditions, braces inside regions, parens/brackets suppression, legacy same-line closers, end markers, leading infix, errors |
| `val`/`var`/`def`, `if`/`while`, lambdas, recursion, integer and string arithmetic, `println` run in both syntaxes | Task 10 fixtures `01`–`06` with `-indent`/`-braces` variants where syntax differs; Task 8/9 unit tests |
| Deep recursion raises `StackOverflowError` (no crash) | Task 8 StackGuard + `Engine.StackOverflowIsAnErrorAndTheSessionSurvives`; fixture `06-recursion/stack-overflow.scala`; `cli/repl-errors.sh` |
| REPL evaluates expressions and definitions with readline history and multi-line continuation | Task 11 (`cli/repl-basic`, `repl-multiline`, `repl-load`, `repl-history-hygiene`, manual terminal check for history) |
| `examples/hello.scala` and `examples/fib.scala` run; cold start < 20 ms | Task 13 `cli/examples.sh`, `benchmarks/cold-start.sh` + `RESULTS.md` |
| Fixtures pass, STATUS.md updated, suite green | Task 13 Steps 3–4 |

DESIGN coverage for Phase 1: §1.1 principles (Global Constraints; rooting rules in Task 8);
§2 types parsed and erased (Tasks 3–5, 7), braces + indentation from Phase 1 (Task 2),
integers D1 (Task 8 fast paths), Float = Double D2 (Task 1 suffixes, Task 9), implicits D3
(Task 4 rejection); §3.1 layout and library names (File Structure; `Session` lives in
`protoscala_repl`); §3.2 lexer (Tasks 1–2); §3.3 parser (Tasks 3–4); §3.4 desugar rows
needed now (Task 5; `for`, interpolation, `match`, `a(i) = v` are later phases);
§3.5 32-bit words, 24-bit operand, `EXTEND`, per-function modules, dedup pool, line table,
per-activation captures, numbering fixed and mirrored in STATUS (Tasks 6, 7, 13); §3.6
recursive VM with one context per frame, stack guard, dedicated large-stack thread,
`switch` dispatch, SmallInteger fast paths with protoCore promotion, thread-local active
call context saved/restored (Task 8); §4.1 value table incl. `unit` singleton and `null` =
`PROTO_NONE` probed with `hasOwnAttribute` (Tasks 8–9); §4.6 no `ProtoTuple` (Global
Constraints; varargs/captures are `ProtoList`); §5.1 universal `apply` for non-function
callees (Task 8 `invoke`); §10 testing strategy incl. GC pressure (`cli/gc-pressure.sh`)
and self-reporting benchmarks (Task 13).

Placeholder scan: every code step shows code; test expectations are concrete; the only
values the executor fills in are measurements and dates (`RESULTS.md`, `CHANGELOG.md`),
which cannot be known in advance.

Type consistency (names used across tasks): `Token`/`TokenKind`/`SourcePos` (T1),
`LexError`/`tokenize` (T2), `ParseError`/`parseSource`/`parseExpressionSource`/`dump` (T3–4),
`desugar`/`desugarExpr` (T5), `BytecodeModule`/`Op`/`kMaxOperand` (T6),
`GlobalTable`/`BindingKind`/`Compiler`/`CompiledUnit`/`UnitMode`/`CompileError` (T7),
`ScalaError`/`StackOverflowError`/`Runtime`/`RuntimeLayout`/`ExecutionEngine::run|invoke|send|callTopLevel`/`activeCallContext`/`show`/`valuesEqual`/`typeName`/`toScalaString` (T8, `callTopLevel` added in T10),
`installPrimitives`/`builtinGlobalNames` (T9), `Session`/`EvalOutcome`/`EvalStatus` (T10–11),
`runRepl` (T11).

---

## Open questions for the maintainer

Each question has the provisional behaviour this plan implements (so execution is not
blocked) and is reversible; provisional deviations are recorded in STATUS.md (Task 13).

- **Q1 — Indentation inside parentheses.** DESIGN §3.2 says no layout tokens inside `(...)`
  and `[...]`. Scala 3 does allow an indented, multi-statement lambda body inside an
  argument list (`xs.foreach(x =>` newline + indented statements). *Provisional:* follow
  DESIGN; such bodies need braces (single-expression bodies spanning lines work). D17.
- **Q2 — Script mode and top-level initialisation.** Scala 3 `.scala` files allow only
  definitions at top level and initialise a file's top-level vals on first access.
  *Provisional:* top-level expression statements are accepted and run in order; top-level
  vals are initialised eagerly, in order, before `@main`. D9.
- **Q3 — Bare reference to a method.** *Provisional:* a reference to a `def` with parameter
  lists and no arguments is the function value (Scala 3 eta-expansion); for `def f()` Scala
  3 would require `f()`. D10.
- **Q4 — Boxing of captured variables.** Captured `var`s and local `def`s live in a mutable
  protoCore object (a Cell) so closures share them. Mutable objects enter protoCore's
  mutables tree, which is scanned at stop-the-world (P6). *Provisional:* Cells as described;
  alternative would be a new protoCore "ref" type.
- **Q5 — Non-local `return`.** *Provisional:* `return` inside a lambda is a compile error
  (Scala 3 deprecates non-local returns). D11.
- **Q6 — By-name parameters (`x: => T`).** Types are erased, but by-name changes evaluation
  order and needs callee knowledge at the call site. *Provisional:* parsed; the compiler
  rejects them ("not supported yet"). Which phase should implement them?
- **Q7 — `@main` signatures.** *Provisional:* at most one `@main` per file, with no
  parameters or a single `args: String*`; Scala 3's typed `@main` parameters (parsed from
  the command line by `FromString`) are rejected.
- **Q8 — `import` in Phase 1.** *Provisional:* parsed and ignored (UMD resolution arrives
  in Phase 6). Should an import of an unknown path be an error already?
- **Q9 — Error names and output format.** *Provisional:* unqualified Scala/Java class names
  (`ArithmeticException`, `NullPointerException`, `ClassCastException`, `NoSuchMethodError`
  for a missing member, `IllegalArgumentException` for wrong arity,
  `UninitializedFieldError` for a global used before initialisation); stderr lines
  `file:line:col: error: msg` (parse/compile) and `file:line: error: Class: msg` (run time);
  exit code 1. D14.
- **Q10 — REPL look.** *Provisional:* prompts `scala> ` / `     | `; echoes `val x = v`,
  `var x = v`, `lazy val x`, `def f`, `val resN = v` (no types, since types are erased);
  `()` results are not echoed.
- **Q11 — Strings.** *Provisional:* `length`, `charAt`, `substring`, `indexOf` count code
  points (JVM: UTF-16 units; they differ only outside the BMP); `toUpperCase`/`toLowerCase`
  and `Char.toUpper`/`toLower` map ASCII only. D13.
- **Q12 — `>>>`.** With arbitrary-precision integers (D1) there is no fixed width.
  *Provisional:* `UnsupportedOperationException`. `<<` likewise does not wrap
  (`1 << 60` is 2^60 instead of Scala's `1 << 28`). D15 (and D1).
- **Q13 — Tabs.** *Provisional:* a file whose indentation mixes tabs and spaces is
  rejected (Scala 3 compares indentation prefixes). D16.
- **Q14 — `:` at end of line.** *Provisional:* always `ColonEol` (opens a region when the
  next line is deeper), so `val x:` newline `Int = 1` is rejected. D18.
- **Q15 — Varargs representation.** *Provisional:* a `ProtoList` (Scala `List`), so
  `toString` prints `List(...)` where Scala prints `ArraySeq(...)`. D12.
- **Q16 — Cold-start check in CI.** *Provisional:* `benchmarks/cold-start.sh` is run by hand
  and recorded in `benchmarks/RESULTS.md`, not a ctest case (timing is machine-dependent).
- **Q17 — License.** *Provisional:* MIT, copied from protoClojure (`CMakeLists.txt` already
  installs a `LICENSE` that does not exist yet).
- **Q18 — `--version` content.** ROADMAP Phase 0 says `--version` prints the version *and
  the protoCore it links*; the binary prints only `protoScala X.Y.Z` and
  `tests/cli/version.sh` requires exactly that. This plan does not change it.
- **Q19 — `x op= y`.** *Provisional:* for an identifier `x` always `x = x op y` (no `op=`
  members exist yet); when collections with `+=` arrive this must first look for an `op=`
  member (DESIGN §3.4 "no `op=` member").
- **Q20 — `lazy val` thread safety.** *Provisional:* the Lazy holder is initialised without
  synchronisation (Phase 1 is single-threaded); Phase 5 (actors) needs a CAS-based
  protocol.
- **Q21 — GC safepoint at loop back-edges (R1).** *Provisional:* `JUMP_BACK` calls the
  existing public `ProtoContext::safepoint()` (the STW handshake plus young-generation
  submission; protoPython uses it in its dispatch loop, protoST at quiescent points). At a
  back edge every live value of the frame is in a slot. Without it, an allocating `while`
  loop retains all its garbage until the frame returns and a tight loop can stall a
  stop-the-world pause. This touches R1, which DESIGN assigns to the maintainer.
- **Q22 — Hosting `Session` in `protoscala_repl`.** DESIGN §3.1 lists five libraries; the
  script runner and the REPL share `Session`, which this plan places in `protoscala_repl`
  rather than adding a sixth library (`protoscala_driver`).
