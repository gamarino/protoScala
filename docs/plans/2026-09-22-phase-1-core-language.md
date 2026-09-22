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

