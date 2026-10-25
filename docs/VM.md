# The protoScala virtual machine

This document describes the bytecode virtual machine that runs protoScala
programs: how source becomes bytecode, how a frame is laid out, every opcode
in the instruction set, and worked examples. Every bytecode listing in it is
real output of `protoscala --disassemble`, checked by the ctest
`docs/vm-examples`; none is written by hand.

Contents:

- [Start here](#start-here)
- [1. Overview](#1-overview)
- [2. Opcode reference](#2-opcode-reference)
- [3. Worked examples](#3-worked-examples)
- [4. How to add an opcode](#4-how-to-add-an-opcode)
- [5. How to add a primitive](#5-how-to-add-a-primitive)
- [6. Known oddities](#6-known-oddities)

The call path described here is `main` at commit `3ac106e8`. A local branch,
`perf/result-in-caller-slot`, changes how a call's result is rooted (in the
caller's slot instead of a `ReturnReference`); sections 1.3 and 1.5 describe
`main`. The opcode table in [STATUS.md](STATUS.md#opcode-table-mirrors-srccompileropcodesh-design-35)
is a summary; this document is the full reference.

## Start here

### Reading order

Read the source in the order a program travels through it:

| Step | Files | Key functions | What happens |
|---|---|---|---|
| 1. Driver | `src/main.cpp`, `src/repl/Session.cpp` | `main`, `runJob`, `Session::runScript`, `Session::evaluate`, `Session::disassemble` | A script is read, compiled and run **as one compilation unit**: the whole file becomes one top-level module, which runs once; then its `@main` method (or `App` object) is called. The REPL compiles each input as a unit of its own. |
| 2. Lexer and parser | `src/frontend/Lexer.cpp`, `src/frontend/Layout.cpp`, `src/frontend/Parser.cpp`, `src/frontend/AST.h` | `parseSource`, `Parser::parseCompilationUnit`, `Parser::parseExpr` | Text becomes an AST. `Layout.cpp` turns Scala 3's significant indentation into region tokens, so the parser sees one grammar for braces and indentation. |
| 3. Desugaring | `src/frontend/Desugar.cpp` | `desugar`, `Desugarer::expr`, `infix`, `forExpr`, `foldGenerators`, `defDef` | Infix operators become method selections, `for` comprehensions become `withFilter` / `flatMap` / `map` / `foreach` calls, case classes get their synthesised `apply` / `unapply`, `o.x = v` becomes a setter send. |
| 4. Compiler | `src/compiler/Compiler.cpp`, `CompilePatterns.cpp`, `CompileTemplates.cpp`, `Compiler.h`, `GlobalTable.h` | `Compiler::compileUnit`, `compileExpr`, `compileApply`, `compileFunction`, `compileStats`, `compileMatch`, `compilePattern`, `compileTry`, `compileTemplate`, `compileConstructor`, `compileNewOf`, `resolve`, `captureInto` | Expressions, definitions, patterns and templates become instructions. Names resolve, in order, to a local, a member of the enclosing template, or a global. The compiler tracks the operand-stack depth of every instruction it emits (`Compiler::emit`), which gives each function's `maxStack`. |
| 5. Bytecode | `src/compiler/Opcodes.h`, `src/compiler/BytecodeModule.h`, `BytecodeModule.cpp` | `BytecodeModule::emit`, `emitJump`, `patchJumpTo`, `emitJumpBack`, `addBlock`, `addHandler`, `linkSymbols`, `disassemble`, `opName` | The instruction set and the module: code words, a parallel line table, a constant pool, nested function bodies (blocks), the exception-handler table, frame metadata. |
| 6. VM | `src/runtime/ExecutionEngine.h`, `ExecutionEngine.cpp`, `src/runtime/OpcodeOps.h` | `ExecutionEngine::execute`, `runFrame`, `runLoop`, `invoke`, `dispatch`, `callMember`, `callNative` | The dispatch loop, one `ProtoContext` per frame. `OpcodeOps.h` holds the body of every non-trivial opcode exactly once; the interpreter and the transpiler (`protoscalac`) both call it. |
| 7. Primitives and prelude | `src/runtime/Primitives.cpp`, `CollectionPrimitives.cpp`, `ProductPrimitives.cpp`, `PrimitiveSupport.h`, `lib/prelude.scala`, `src/runtime/Prelude.cpp`, `src/support/BuiltinNames.cpp` | `installPrimitives`, `installAll`, the `PRIM` macro, `builtinGlobalNames` | Native methods written in C++ are installed on the prototypes of the built-in types and on the globals object. Most of the library (`Option`, `Either`, `Try`, the exception classes, ...) is Scala code in `lib/prelude.scala`, compiled at start-up or loaded from a precompiled image. |

`BytecodeModule::disassemble` (in `BytecodeModule.cpp`, with `commentFor`
and `formatConst` above it) prints a module; reading it alongside
`Opcodes.h` is a quick way to learn the operand of every opcode.

### Trying things with `--disassemble`

```
protoscala --disassemble script.scala
```

compiles the script, prints the bytecode of its top-level module and of
every function body it holds, nested ones included, and exits **without
running it**. The prelude is not listed. Example 01 in section 3 shows the
listing of `println(1 + 2)`; every example there is such a listing.

How to read a listing:

- `function <top>` is the compilation unit's top-level module. Every other
  `function` line is a block (a nested body), listed after the instructions of
  the function that owns it and indented one level deeper. `MAKE_FN 1 ; ->
  block 1` builds a function value from the owner's block 1, counting the
  blocks listed directly under the owner from 0.
- The header gives the frame layout: `arity` (parameters, `this` included for
  a method), `variadic` (the last parameter is a repeated one, `xs: Int*`),
  `method` (slot 0 is the receiver), `locals` (every other slot: `val`s and
  `var`s, captures, pattern temporaries, catch slots), `stack` (operand-stack
  capacity) and `captures` (how many values the function captures).
- `captures:` maps each captured value from the enclosing frame's slot that
  `MAKE_FN` reads to the slot of this frame it lands in.
- Each instruction line is: position (instruction words), `L` and the source
  line, the opcode, its operand, and a comment that decodes the operand: a
  constant, a global's name, a send site (`name/argc`), a jump's absolute
  target (`-> 7`), a block index, a type code.
- `handler [start, end) -> target depth=d slot=s catch|finally` lines are
  the function's exception-handler table, in search order (1.7).
- A default-argument block (`greet$default$1`) is listed under its function
  like any block, but no `MAKE_FN` refers to it: the callee's prologue runs it
  (1.5).

The disassembler compiles against a copy of the session's global table and
has no module loader, so a script that imports another protoScala module may
not compile under `--disassemble` although it runs.

## 1. Overview

### 1.1 Pipeline

```
source --parseSource--> AST --desugar--> AST --Compiler::compileUnit--> BytecodeModule tree
  --linkSymbols--> (names interned) --ExecutionEngine::run--> top-level statements run
  --> @main (or the App object) is called
```

`Session::evaluate` (`src/repl/Session.cpp`) is the single entry for scripts
and the REPL. A unit's globals are committed only when it compiles and runs
without error.

`Compiler::compileUnit` lays out the top-level module in this order:

1. imports (they emit no code; a name is bound at compile time);
2. every class, trait and object, in dependency order (`sortTemplates`): the
   members are compiled as blocks, `MAKE_CLASS` builds the prototype and
   `STORE_GLOBAL` binds it under the type key (`@Point`); an object also gets
   a lazy holder bound under its term name (`Point`, example 09);
3. every top-level `def` (`MAKE_FN` + `STORE_GLOBAL`) and `lazy val`
   (`MAKE_FN` + `MAKE_LAZY` + `STORE_GLOBAL`), before any statement runs, so
   definitions may be used before the line that defines them;
4. the remaining statements and `val`s, in source order (`STORE_GLOBAL` for a
   `val`, `POP` for a statement's value);
5. `PUSH_UNIT`, `RETURN`.

Unlike protoClojure, a top-level `val` is a global, not a local: the top-level
module has local slots only for compiler temporaries (a `match`, a `try`, an
`asInstanceOf`).

### 1.2 Instruction encoding and the module

An instruction is one 32-bit word (`Instr`): the opcode in the low 8 bits, an
unsigned 24-bit operand in the high 24 bits (`kOperandShift = 8`,
`kMaxOperand = 16,777,215`). A larger operand, up to 2^48 - 1, is emitted as
an `EXTEND` word carrying bits 24..47 followed by the instruction carrying
bits 0..23 (`BytecodeModule::emit`); beyond that `emit` throws
`std::length_error`, reported as a compile error. Jumps are never extended:
`patchJumpTo` and `emitJumpBack` throw `function body too large` instead.

- **Positions and jumps** count instruction words. A jump's operand is
  relative to the instruction **after** the jump: `JUMP`, `JUMP_IF_FALSE` and
  `JUMP_IF_TRUE` go to `pc + 1 + operand`; `JUMP_BACK` goes to
  `pc + 1 - operand`.
- **Line table:** one source line per code word (`lines_`), the `L` column
  of a listing. The debugger and error messages read it; there is no `LINE`
  opcode.
- **Constant pool** (`BytecodeModule::Const`), de-duplicated by kind:
  `Int`, `BigInt` (digits and base, rebuilt by each `PUSH_CONST`), `Double`
  (by bit pattern), `String`, `Char` (a code point), `Symbol` (a global or
  attribute name), `SendSite` (name, argument count, optional D5 fallback
  name), `Names` (a list of attribute keys), `ClassSpec` (one class: display
  name, type key, parent count, member keys, product fields, flags; never
  de-duplicated), `SuperSite` (name, argument count, the defining template's
  key, `exact` for `super[T]`), `KwSendSite` (name, positional count, keyword
  names, fallback). `linkSymbols` interns every name a constant holds before
  the module runs; interned symbols are perennial, so a module holds no
  collectable reference.
- **Blocks:** every function body (a `def`, a lambda, a method, a by-name
  argument, a lazy thunk, a default-argument expression) is a
  `BytecodeModule` of its own, owned by the module that contains it
  (`addBlock`). `MAKE_FN`'s operand is a block index. Modules live as long as
  the session; a function value points at its module by address.
- **Frame metadata** of a body: `arity`, `isVariadic`, `isMethod`,
  `isParamless` (`def p = e`, no parameter list), `localCount`, `maxStack`,
  `captureSpecs` (parent slot, local slot), `paramNames` (for named
  arguments), the default blocks of its parameters, the handler table, slot
  names for the debugger, and `nativeEntry` (a transpiled body, 1.8).
- **`maxStack`** is the deepest operand stack the compiler's own accounting
  saw (`Compiler::adjust`), not a flow analysis of the finished code. Every
  emission site states its stack effect; a mistake there is a frame that is
  too small. `adjust` throws `compiler: operand stack underflow` if the depth
  ever goes negative.
- **No rewriting pass.** protoScala has no peephole optimiser, specialiser,
  fused opcodes or second interpreter tier: the code the compiler emits is
  the code the VM runs (and the code the transpiler translates).

### 1.3 Frames

`ExecutionEngine::execute` runs one activation of one body. It creates
exactly **one `ProtoContext` per activation** (the frame), chained to the
caller's context through `previous`. The context's automatic locals hold the
whole frame, so every value the frame holds is in a slot the garbage
collector traces:

```
slot 0 .. arity-1                    parameters; for a method, slot 0 is `this`;
                                     a variadic function's last parameter is a List
slot arity .. arity+localCount-1     vals, vars, captures, cells, pattern and catch temporaries
slot arity+localCount ..             operand stack, capacity maxStack
```

Slots are allocated in source order and never reused within one body. On
entry `execute`:

1. checks the native stack (`checkNativeStack`: `StackOverflowError` before
   the thread's stack runs out; recursion nests on the native stack);
2. checks the argument count (`IllegalArgumentException: wrong number of
   arguments for f: expected 1, got 2`), allowing fewer arguments when named
   arguments or defaults can fill the rest;
3. copies the positional arguments into slots 0.., builds the rest `List` of
   a variadic function;
4. copies the captures from the function value's `__captures__` list into
   their slots (`captureSpecs` order);
5. binds named arguments and runs the default blocks of the parameters still
   missing (`bindKeywordsAndDefaults`, 1.5);
6. runs the body (`runFrame`, which runs `runLoop`).

`RETURN` answers the top of the stack. Every body ends in `RETURN`; there is
no implicit return at the end of the code. The frame's result is stored in
`frame.returnValue`; protoCore's context destructor anchors it in the
caller's young generation with a `ReturnReference` before submitting the
frame's own young generation to the collector.

Some captured bindings are **boxed**: their slot holds a mutable Cell
(`MAKE_CELL`) and reads and writes go through `PUSH_CELL` / `STORE_CELL`, so
the closure and the frame share them (example 07). `CaptureAnalysis` (in
`Compiler.cpp`) decides this before a body is compiled: a `var` read from a
nested function, a local `def` read from a nested function (local `def`s are
hoisted, so one may be captured before it is stored), and a `val` or
`lazy val` read from a nested function when a hoisted function lies between
the two. Parameters are never boxed.

### 1.4 Function values and classes

A function value is a protoCore object built by `MAKE_FN`:

| Attribute | Content |
|---|---|
| prototype | `functionProtoFor(arity)`: the `Function0`..`Function22` prototype of the prelude |
| `__code__` | the address of the body's `BytecodeModule`, as a SmallInteger (`compiledModuleOf` reads it back) |
| `__captures__` | a `List` of the captured values, present only when the body captures |
| `__self__` | the receiver, only in a bound method (an eta-expanded `obj.m`, `bindMethod`) |

- **Captures** are copied at `MAKE_FN` time: the compiler pushes each
  captured slot of the enclosing frame (`PUSH_LOCAL parentSlot`) right before
  `MAKE_FN`, which pops them into the list. For a boxed binding the slot holds
  the Cell, so the copy shares the variable. A value captured through several
  nested functions is captured by each intermediate one (`captureInto`).
- **Methods capture nothing** (Design note 9): a method's `FunctionState` has
  no parent, and `execute` raises a `std::logic_error` if a module that needs
  captures is called without them.
- **Classes.** `MAKE_CLASS` builds a class prototype: a mutable object whose
  parent chain is the class's linearization (the pushed parent prototypes),
  carrying the member function values, the membership marker (`@Point` =
  true), `__name__`, and, for a case class, `__prefix__` and the product
  `__fields__`. An instance is `cls->newChild(...)`, immutable unless the
  class (or an ancestor) has a `var` field. A constructor (`<init>`) is a
  method that receives the fresh instance in slot 0 and returns it: on an
  immutable instance every `STORE_FIELD` produces a new version of `this`,
  written back to slot 0.
- **Objects** are lazy holders: the term name is bound to `MAKE_LAZY` of a
  thunk that runs `NEW` on the object's class, and every read is
  `PUSH_GLOBAL` + `FORCE`.
- **Private members** are stored under a class-qualified key
  (`Square::side`, deviation D5); a send site carries the plain name as a
  fallback for a receiver that is not an instance of the compiling class.

### 1.5 Calling conventions

This section describes `main` at `3ac106e8`.

**`CALL n`** expects `[callee, a1 .. an]` on the stack (callee deepest) and
replaces them with the result. `ExecutionEngine::invoke` dispatches:

1. `null`: `NullPointerException: cannot call null`.
2. **A protoScala function** (`compiledModuleOf` finds `__code__`): `execute`
   runs it in a new frame. A bound method (`__self__`) first gets a
   **scope context** holding `[self, a1..an]` (ExecutionEngine.cpp:215), then
   the frame.
3. **A native method** (a protoCore method cell): `callNative` creates a
   **scope context** (ExecutionEngine.cpp:165), copies the arguments into its
   slots and calls the C function with a `ProtoArguments` view over them.
4. **Anything else**: `send(callee, "apply", args)`, Scala's universal
   `apply` (DESIGN §5.1). `send` creates a scope context
   (ExecutionEngine.cpp:238) for `[receiver, args]`.

**`SEND site`** and **`SEND_APPLY site`** expect `[receiver, a1 .. an]`. The
site gives the member name and `n`. `SEND` is a member written without an
argument list (`xs.head`, `c.count`) and the compiler's own sends (setters,
`unapply`, `toDouble`); `SEND_APPLY` is a member written with one
(`xs.max(9)`, `c.inc()`). `ops::sendNamed` applies the D5 fallback, then
`dispatch`:

1. `null` receiver: `NullPointerException: cannot invoke 'm' on null`.
2. The member is looked up on the receiver, prototype chain included
   (`getAttribute`). Missing: a protoST object gets a Smalltalk message
   (D135); `getMessage` on a foreign error reads its `message`; otherwise
   `NoSuchMethodError: value m is not a member of T` (or `Reassignment to val
   x` for `x_=`). A member that exists and holds `null` answers `null` to
   `SEND` with no arguments.
3. `SEND_APPLY` of a parameterless method (`def p = e`) runs it and applies
   the result (`c.g()` where `def g = () => n`).
4. `callMember`: a native method is called through `callNative` (a scope
   context); a protoScala method runs in a new frame with the receiver in
   slot 0, no extra context; `obj.m` without an argument list on a method
   with parameters is an eta-expansion (`bindMethod`); a function-valued
   field is answered (`SEND`) or applied (`SEND_APPLY`); a `lazy val` member
   is forced; any other field is answered, or applied through its `apply`.

**`CALL_KW site`** and **`SEND_KW site`** carry named arguments: `[callee or
receiver, a1..an, v1..vm]`, the site naming the `m` keywords. The VM builds a
`ProtoKeywordArguments` of (interned parameter name, value) pairs over the
operand-stack slots, without allocating, and the **callee** binds them
(`bindKeywordsAndDefaults`): an unknown name, a duplicate and a missing
argument each raise `IllegalArgumentException` naming the parameter. Both
create a scope context for every call (ExecutionEngine.cpp:677, 722; a bound
method adds a second one at 734). A native method receives the pairs in its
fifth parameter.

**Defaults** are bound by the callee too: a call with fewer positional
arguments than parameters, to a function with defaults, runs the default
block of each missing parameter in the callee's prologue, in parameter
order. A default block mirrors the callee's slot prefix (parameters and
captures) as its own parameters, so it can read earlier parameters
(example 08).

**`CALL_SPREAD n`** and **`NEW_SPREAD site`** (`f(xs*)`, `new C(xs*)`) copy
the `n` fixed arguments and the list's elements into a scope context
(ExecutionEngine.cpp:1210, 1349) and call as `CALL` / `NEW`.

**`NEW site`**, **`INVOKE_INIT site`** and **`SEND_SUPER site`** are the
object-model calls (section 2.8): `instantiate` creates the instance with
`cls->newChild` and runs the constructor named by the site with the
instance in slot 0; `INVOKE_INIT` runs a superclass constructor on the same
instance; `SEND_SUPER` finds the next definition of a member after the
defining template in the receiver's linearization.

**C++ code calling Scala** (`foldLeft`, `map`, actors, `@main`) goes through
`ExecutionEngine::invoke`, `send` and `construct`, with the arguments rooted
by the caller; `callTopLevel` installs the thread's `ActiveCallContext`
first.

### 1.6 Safepoints and rooting

protoCore reclaims nothing a context has not handed over: a context's
allocations are chained on its young generation, which reaches the collector
when the context is destroyed or from `ProtoContext::safepoint()`. The VM
calls `safepoint()` at exactly one instruction:

- **`JUMP_BACK`** (every `while` iteration), on every execution, with no
  stride: a `while` loop is one frame, and its garbage would otherwise never
  reach the collector (DESIGN Q21; `ops::safepoint`).

There is no poll at frame entry. A frame's young generation is submitted
when its context is destroyed, and protoCore itself parks a thread for a
stop-the-world at allocation refills and in the context destructor.
Recursion therefore reaches the collector at every return; whether a long,
allocation-free recursion can delay a stop-the-world has not been measured
for this document.

Rooting inside the VM follows protoCore's EMBEDDER-CONFORMANCE rule 3 (no
`ProtoObject*` held only in a C++ local across an allocation or a
safepoint): operands stay in their stack slots until the result replaces
them (`base[0] = r`), and every opcode body in `OpcodeOps.h` that allocates
writes its intermediate values back into slots (`concat`, `uncons`,
`unapplyFieldsWith`).

**Cooperative suspension.** An actor's `await` suspends by throwing
`FutureYield` through the VM frames. Each call opcode records, in
`pendingBase`, the stack index its result will be written to; a frame that
catches `FutureYield` with a call pending appends itself (module, pc, live
slots) to the actor's snapshot, and `resumeFrames` rebuilds the chain later.
A `FutureYield` that reaches a frame with no call pending becomes
`UnsupportedOperationException: await is not supported here`.

### 1.7 Exceptions

Exceptions cost nothing until something throws: the protected regions live
in the module's **handler table**, not in the instruction stream. An entry is
`[startPc, endPc) -> handlerPc`, the operand-stack depth at the `try`, the
slot the caught value is written to, and its kind (`catch` or `finally`).
Nested `try`s are appended before enclosing ones, and a `try`'s catch entry
before its finally entry, so table order is search order.

When a C++ exception leaves an instruction, `runLoop` records the faulting
pc, translates library exceptions (`std::invalid_argument` to
`IllegalArgumentException`, `std::out_of_range` to
`IndexOutOfBoundsException`, `std::overflow_error` to `ArithmeticException`,
`std::bad_alloc` to `OutOfMemoryError`, `std::runtime_error` to
`RuntimeException`; a `std::logic_error` is a VM or compiler defect and is
never caught, D74) and rethrows. `runFrame` searches the handler table at
that pc:

- a language value (`proto::ProtoException`, which every `throw` raises) is
  written into the handler's slot;
- a native error (`ScalaError`) is first materialised as an instance of the
  prelude class of the same name (`materialiseError`), only when a handler
  exists;
- the operand stack is cut back to the handler's depth and execution
  continues at the handler, **outside** the C++ catch clause, so a handler
  body can itself throw, be caught by an enclosing entry of the same frame,
  or suspend.

With no matching entry the exception leaves the frame, rethrown after the
catch clause completes (required for MSVC's unwinding). The value is **not**
pushed: a handler body begins by reading its slot. The layout compiled by
`Compiler::compileTry`:

```
      <body>                     catch entry covers the body
      JUMP after
catch:                           (the value is in slot s)
      <case 1 test on slot s>, JUMP_IF_FALSE next, <case 1 body>, JUMP after
next: ...
      RETHROW s                  no case matched
after:                           finally entry covers the body and the cascade
      JUMP normal
cleanup:
      <finally>, POP, RETHROW s
normal:
      <finally>, POP
```

The finally body is compiled twice, once per path, plus once more before
every `return` that leaves the `try` (the `return` path is excluded from the
entries' ranges as "holes"). `RETHROW` raises the original C++ exception
object when the frame caught it (protoCore rule E3), so a foreign runtime's
exception keeps what it carries; a `throw e` of a caught `e` does the same.

### 1.8 The second consumer: `protoscalac`

The transpiler `protoscalac` (`src/compiler/TranspilerMain.cpp`,
`CppEmitter.cpp`, specification in
[PROTOSCALAC_SPECIFICATION.md](PROTOSCALAC_SPECIFICATION.md)) consumes the
same `BytecodeModule` tree and emits C++: each opcode becomes straight-line
code over the frame's slots that calls the opcode's body in `OpcodeOps.h`
through the `protoScala::gen::` forwards (`src/runtime/GeneratedSupport.cpp`).
A transpiled body is a `BytecodeModule` with no code words and a
`nativeEntry`; `execute` calls the entry instead of running `runLoop`, so
callers cannot tell the two apart. Every opcode in section 2 is therefore
also a contract for the emitter.

## 2. Opcode reference

There are **61 opcodes** (`src/compiler/Opcodes.h`): values 0 to 39, 64 to
82, 96 and 97. Values 40..63, 83..95, 98..127 and 128..159 are reserved. The
enum, the name table (`opName`) and the VM's `switch` (`runLoop`) agree: each
lists the same 61. The compiler emits 59 of them directly; `EXTEND` is emitted
by `BytecodeModule::emit` for an operand above 2^24 - 1, and `NOP` is never
emitted (section 6). The examples of section 3 show all 59.

Notation: the stack effect is `before -> after`, top of stack on the right.
"Errors" names what the instruction itself raises; a `std::logic_error` is an
internal error, impossible in compiler output. "Allocates" means the
instruction may allocate in the frame's context.

### 2.1 Constants and the stack

| Op | # | Operand | Stack | Semantics | Emitted by |
|---|---|---|---|---|---|
| `NOP` | 0 | none | `->` | Does nothing. | never (section 6) |
| `EXTEND` | 1 | bits 24..47 of the next operand | `->` | Combined with the next word when the VM decodes it; never dispatched on its own. | `BytecodeModule::emit`, for any operand above 16,777,215 |
| `PUSH_CONST` | 2 | constant index | `-> v` | Pushes an `Int`, `BigInt`, `Double`, `String` or `Char` constant as a protoCore value (`ops::constantOf`). A string is a new protoCore string each time and a `BigInt` is rebuilt from its digits: both allocate. A name constant raises `std::logic_error`. | literals (`compileExpr`), interpolation pieces, list-pattern lengths and indices |
| `PUSH_UNIT` | 3 | 0 | `-> ()` | Pushes the Unit value. | a block without a final expression, `if` without `else`, `while`, an assignment, the end of `<top>`, the unreachable slot after `throw` |
| `PUSH_NULL` | 4 | 0 | `-> null` | Pushes `null` (`PROTO_NONE`). | `null` |
| `PUSH_TRUE` | 5 | 0 | `-> true` | | `true`; the short-circuit result of `||` |
| `PUSH_FALSE` | 6 | 0 | `-> false` | | `false`; the short-circuit result of `&&` |
| `POP` | 7 | 0 | `v ->` | Drops the top. | a statement's value; a `while` body's value; a `finally` body's value |
| `DUP` | 8 | 0 | `v -> v v` | Duplicates the top. | a type test against `Throwable` or `Exception` (`compileTypeTest`, example 20) |

### 2.2 Locals and cells

| Op | # | Operand | Stack | Semantics | Emitted by |
|---|---|---|---|---|---|
| `PUSH_LOCAL` | 9 | slot | `-> v` | Pushes the slot (a parameter, local, capture or temporary). For a boxed binding it pushes the Cell itself. | a local name; before `MAKE_FN` for each capture; pattern code; constructors |
| `STORE_LOCAL` | 10 | slot | `v ->` | Pops into the slot. | `val`/`var` of an unboxed local, assignment, pattern bindings and temporaries, the match scrutinee |
| `MAKE_CELL` | 11 | slot | `->` | Stores a new mutable Cell in the slot. Allocates. | `compileStats`, at the start of a block, for each boxed `val`, `var` or local `def` |
| `PUSH_CELL` | 12 | slot | `-> v` | Pushes the value held by the Cell in the slot. | a read of a boxed binding (`loadLocal`) |
| `STORE_CELL` | 13 | slot | `v ->` | Pops into the Cell in the slot. | a write of a boxed binding (`storeLocal`) |

### 2.3 Globals

A global is an own attribute of the globals object, keyed by an interned
symbol. Each definition has its own key (`area`, an overload `area#1`, a type
`@Point`, an object's type `@Point.type`), so a REPL redefinition does not
change code compiled earlier.

| Op | # | Operand | Stack | Semantics | Emitted by |
|---|---|---|---|---|---|
| `PUSH_GLOBAL` | 14 | `Symbol` constant | `-> v` | Pushes the global. Errors: `UninitializedFieldError: x is used before it is initialised` when it has no binding yet (a global holding `null` pushes `null`). | a global name; a class prototype for `NEW` and `MAKE_CLASS`; the callee of an overloaded `def` |
| `STORE_GLOBAL` | 15 | `Symbol` constant | `v ->` | Pops into the global. | top-level `def`, `val`, `var`, `lazy val`, class, object; assignment to a top-level `var`; a REPL result |

### 2.4 Control flow

| Op | # | Operand | Stack | Semantics | Emitted by |
|---|---|---|---|---|---|
| `JUMP` | 23 | forward offset | `->` | `pc += operand` (from the next instruction). | `if`, `match`, `&&`/`||`, `try` |
| `JUMP_IF_FALSE` | 24 | forward offset | `b ->` | Pops; jumps when `false`. Errors: `ClassCastException: T cannot be cast to Boolean` for a non-Boolean. | `if`, `while`, `&&`, pattern tests, guards |
| `JUMP_IF_TRUE` | 25 | forward offset | `b ->` | Pops; jumps when `true`; same error. | `||`; `asInstanceOf`; extractor `isEmpty` tests; `Throwable`/`Exception` tests |
| `JUMP_BACK` | 26 | backward offset | `->` | `pc -= operand` (from the next instruction), then `ProtoContext::safepoint()` (1.6). | `while` (`emitJumpBack`) |

### 2.5 Functions, calls and returns

| Op | # | Operand | Stack | Semantics | Emitted by |
|---|---|---|---|---|---|
| `MAKE_FN` | 16 | block index | `c1..ck -> fn` | Pops the block's `k` captures and pushes a function value (1.4). Allocates. | `def`, lambda, method, constructor, by-name argument, lazy thunk, object holder (`compileFunction`) |
| `CALL` | 17 | argc | `f a1..an -> r` | Calls `f` (1.5). Errors: `NullPointerException: cannot call null`; the callee's arity error. | `f(args)` when `f` is a local, global or expression; a parameterless top-level `def` read (`CALL 0`); internal helpers (`__fmt`, `__isForeignObject`, `__isInstanceOf`, `__installExtension`) |
| `CALL_SPREAD` | 18 | n (fixed args) | `f a1..an list -> r` | As `CALL` with the list's elements appended. Errors: `ClassCastException: T cannot be spliced as arguments`. | `f(a, xs*)` |
| `CALL_KW` | 80 | `KwSendSite` constant | `f a1..an v1..vm -> r` | Calls `f` with named arguments (1.5). Errors: `IllegalArgumentException` (unknown, duplicate or missing parameter; `f does not take named arguments`). | `f(x = 1)` where `f` is not a member |
| `SEND` | 19 | `SendSite` constant | `recv a1..an -> r` | Member access or call without an argument list (1.5). Errors: `NullPointerException`, `NoSuchMethodError`. | `x.m`, a member name inside a template (`this.m`), `unary_` operators other than `-`/`!`, setters, `unapply`, `toDouble` widening |
| `SEND_APPLY` | 79 | `SendSite` constant | `recv a1..an -> r` | Member call written with an argument list: calls the member when it is a method, else applies its value (D10). Errors: as `SEND`, plus `NoSuchMethodError: method p does not take parameters`. | `x.m(args)`, `m(args)` inside a template, operators without a fast opcode (`a / b`, `->`) |
| `SEND_KW` | 77 | `KwSendSite` constant | `recv a1..an v1..vm -> r` | Member call with named arguments. Errors: as `CALL_KW`; `m is not a method of T, so it takes no named arguments`; a Smalltalk message takes none. | `x.m(y = 1)`, `m(y = 1)` inside a template |
| `RETURN` | 20 | 0 | `v ->` | Ends the frame with the top of the stack. | the end of every body; `return e` (after the enclosing `finally` bodies) |
| `FORCE_THUNK` | 38 | 0 | `v -> v'` | When `v` is a zero-argument protoScala function, calls it; any other value passes through (D47, D53). | every read of a by-name parameter or by-name constructor field |

### 2.6 Laziness

| Op | # | Operand | Stack | Semantics | Emitted by |
|---|---|---|---|---|---|
| `MAKE_LAZY` | 21 | 0 | `thunk -> lazy` | Wraps a zero-argument function in a mutable lazy holder. Allocates. | `lazy val` (`compileLazyThunk`); an `object` holder |
| `FORCE` | 22 | 0 | `v -> v'` | A lazy holder answers its value, running the thunk the first time and storing the result in the holder; any other value passes through. | a read of a `lazy val` or an `object` |

### 2.7 Arithmetic and comparison fast paths

The compiler emits these for `a op b` with exactly one argument that is not a
splice, whatever the operand types (`isFastBinary`); there are no static
types. Each has a SmallInteger fast path and falls back to the general case.

| Op | # | Operand | Stack | Semantics | Emitted by |
|---|---|---|---|---|---|
| `ADD` | 27 | 0 | `a b -> r` | Two SmallIntegers: the sum, promoted to a large integer on overflow (D1). Else `slowBinary`: a String on either side concatenates; numbers (a `Char` widened to its code point) add; anything else sends `+` to `a`. | `a + b` |
| `SUB` | 28 | 0 | `a b -> r` | As `ADD` for `-`. | `a - b` |
| `MUL` | 29 | 0 | `a b -> r` | As `ADD` for `*`; also `String * Int`. | `a * b` |
| `LT` | 30 | 0 | `a b -> Boolean` | SmallIntegers compare directly; numbers and strings by `partialCompare` (NaN compares false); anything else sends `<`. | `a < b` |
| `LE` | 31 | 0 | `a b -> Boolean` | As `LT`. | `a <= b` |
| `GT` | 32 | 0 | `a b -> Boolean` | As `LT`. | `a > b` |
| `GE` | 33 | 0 | `a b -> Boolean` | As `LT`. | `a >= b`; a `List(a, b, _*)` pattern's length test |
| `EQ` | 34 | 0 | `a b -> Boolean` | Scala `==` (`valuesEqual`: a user `equals` is honoured). No separate fast path: `valuesEqual`'s first branch is one (measured, see the comment in `runLoop`). | `a == b`; literal patterns (`literal == scrutinee`); `List(...)` pattern length |
| `NE` | 35 | 0 | `a b -> Boolean` | `!(a == b)`. | `a != b` |
| `NEG` | 36 | 0 | `a -> r` | SmallInteger negation; a number's `negate`; else sends `unary_-`. | `-a` |
| `NOT` | 37 | 0 | `b -> r` | Boolean negation; else sends `unary_!`. | `!b`; the foreign-value test of a `catch` on `Exception` |
| `CONCAT` | 39 | n (≥ 2) | `v1..vn -> str` | Converts each value with `toScalaString` and joins them with `ProtoString::appendLast` (a rope join). Allocates. Errors: `std::logic_error` for `n < 2`. | `s"..."` and `raw"..."` interpolation (D54) |

### 2.8 Objects and classes

| Op | # | Operand | Stack | Semantics | Emitted by |
|---|---|---|---|---|---|
| `MAKE_CLASS` | 64 | `ClassSpec` constant | `p1..pk m1..mn -> cls` | Builds a class prototype from `k` parent prototypes (the linearization) and `n` member values (1.4). Allocates. | every class, trait and object (`compileTemplate`) |
| `NEW` | 65 | `SendSite` (constructor key, n) or `KwSendSite` | `cls a1..an [v1..vm] -> obj` | Creates an instance and runs the constructor named by the site. Errors: `IllegalArgumentException: T has no constructor taking n arguments`. | `new C(args)`; `C(args)` of a case class with the synthesised `apply`; an object holder |
| `NEW_SPREAD` | 78 | `SendSite` (constructor key, n) | `cls a1..an list -> obj` | `NEW` with the list's elements appended. Errors: as `CALL_SPREAD`. | `new C(xs*)`, `C(xs*)` |
| `INVOKE_INIT` | 66 | `SendSite` (constructor key, n) | `cls this a1..an -> this'` | Runs the superclass constructor `cls.<init>` on `this` and pushes the (possibly new) instance, which the compiler stores back into slot 0. Errors: `std::logic_error` when the constructor is missing. | the `extends C(args)` call of a constructor (`compileInitCall`) |
| `STORE_FIELD` | 67 | `Symbol` constant | `v ->` | `slot[0] = slot[0].setAttribute(key, v)`. Allocates for an immutable instance. | a `val`/`var` field initialiser in a constructor |
| `STORE_FIELD_IF_NEW` | 81 | `Symbol` constant | `v ->` | As `STORE_FIELD`, skipped when `slot[0]` already has the key: a subclass's `override val` parameter, stored first, wins. | a constructor parameter field, when there is one (or `PROTOSCALA_FIELD_GROUPS=off`) |
| `STORE_FIELDS_IF_NEW` | 82 | `Names` constant (n keys) | `v1..vn ->` | `STORE_FIELD_IF_NEW` for each key, published as one new version of `this` (`ProtoObject::setAttributes`). | constructor parameter fields, two or more (the default) |
| `SET_FIELD` | 68 | `Symbol` constant | `obj v ->` | Writes the field of a mutable instance in place. Errors: `UnsupportedOperationException: cannot assign a field of an immutable object`. | the synthesised setter `x_=` of a `var` field (`compileSetter`) |
| `SEND_SUPER` | 69 | `SuperSite` constant | `this a1..an -> r` | Calls the first definition of the member after the defining template (at `T` for `super[T]`) in the receiver's linearization, found by pointer identity. Errors: `NoSuchMethodError` (`T is not in the linearization of U`; `super.m has no implementation after T`). | `super.m(args)` |
| `MAKE_TUPLE` | 76 | n (2..22) | `a1..an -> tuple` | An instance of `TupleN` with `_1.._n`. Allocates. Errors: `std::logic_error` outside 2..22. | `(a, b)`; `TupleN(...)` |

### 2.9 Pattern matching and type tests

| Op | # | Operand | Stack | Semantics | Emitted by |
|---|---|---|---|---|---|
| `TEST_TYPE` | 70 | `TypeCode` | `v -> Boolean` | Tests a built-in type: `Int` (all integers, D29), `Double`, `Boolean`, `Char`, `String`, `Unit`, `List`, `::` (a non-empty list), `Function`, `AnyRef`, `AnyVal`, `Null`, `Any` (non-null), `Nothing` (always false). | type patterns, `isInstanceOf`, `asInstanceOf`, `::` and `List(...)` patterns |
| `TEST_PROTO` | 71 | `Symbol` constant (a type key) | `v -> Boolean` | True when the value answers the class's marker attribute (`@Point`) with `true`, prototype chain included; `null` is false. | class type patterns, case-class patterns, tuple patterns, catch cases |
| `UNAPPLY_FIELDS` | 72 | `Names` constant | `v -> f1..fn` | Pushes the value's own attributes named by the keys (`null` when missing), in order. | case-class and tuple patterns (`extractInto`) |
| `UNCONS` | 73 | 0 | `list -> head tail` | Splits a non-empty list (the compiler tests `::` first). | `h :: t` patterns |
| `MATCH_ERROR` | 74 | 0 | `v -> (throws)` | Raises `MatchError: <v> (of class T)`. | the end of every `match` cascade |
| `CAST_FAIL` | 75 | `String` constant (the type) | `v -> (throws)` | Raises `ClassCastException: T cannot be cast to <type>`. | `asInstanceOf` (`compileInstanceOf`) |

### 2.10 Exceptions

| Op | # | Operand | Stack | Semantics | Emitted by |
|---|---|---|---|---|---|
| `THROW` | 96 | 0 | `v -> (throws)` | Raises the value. A value this frame caught is re-raised as its original C++ exception (E3). Errors: `NullPointerException: throw null`; `IllegalArgumentException: throw expects a Throwable, got T` for a Scala value that is not a `Throwable` (a foreign object is raised as it is, D134). | `throw e` |
| `RETHROW` | 97 | slot | `-> (throws)` | Re-raises the value the handler stored in the slot, as the original exception when this frame caught it. | the end of a catch cascade that matched nothing; the end of a `finally` cleanup |

### 2.11 What has no opcode

- **Collections** are built through calls, not opcodes: `List(1, 2)` is
  `PUSH_GLOBAL List` + `CALL 2` (the native `apply` of the List companion),
  `Map(...)` the same with `->` sends; only tuples have an opcode.
- **Debugging**: no `LINE` or breakpoint opcode. A debugger attached through
  `protoscala --dap` gets a call per instruction (`debug::atInstruction`)
  and reads the line table; when none is attached the loop tests a local
  flag per instruction.
- **Actors and futures** are sends to native methods; values 128..159 stay
  reserved for them (STATUS.md).
- **Imports** emit nothing (Phase 6): a name is bound at compile time.

## 3. Worked examples

Each example is a file in `docs/vm-examples/`; the listing below it is
`protoscala --disassemble` output, compared with the golden file by the ctest
`docs/vm-examples` (which also runs the example). Regenerate them with
`python3 -I docs/vm-examples/disasm_examples.py build_release/protoscala --update`.

### Example 01: a constant and arithmetic, `1 + 2`

<!-- vm-example: 01-arithmetic -->
```scala
println(1 + 2)
```

```text
function <top> arity=0 locals=0 stack=3 captures=0
  0000  L1  PUSH_GLOBAL 0 ; println
  0001  L1  PUSH_CONST 1 ; 1
  0002  L1  PUSH_CONST 2 ; 2
  0003  L1  ADD 0
  0004  L1  CALL 1
  0005  L1  POP 0
  0006  L1  PUSH_UNIT 0
  0007  L1  RETURN 0
```

- 0000: `println` is a global native method; it is pushed first, as the
  callee of the `CALL` at 0004.
- 0001-0003: the two constants and `ADD`. There is no constant folding.
- 0004: `CALL 1` calls `println` through `callNative` (1.5).
- 0005: the statement's value is dropped; 0006-0007: `<top>` returns Unit.

### Example 02: `val` in a function body

<!-- vm-example: 02-val -->
```scala
def area(w: Int, h: Int): Int =
  val a = w * h
  val b = a - 1
  b + 1

println(area(3, 4))
```

```text
function <top> arity=0 locals=0 stack=4 captures=0
  0000  L1  MAKE_FN 0 ; -> block 0
  0001  L1  STORE_GLOBAL 0 ; area
  0002  L6  PUSH_GLOBAL 1 ; println
  0003  L6  PUSH_GLOBAL 0 ; area
  0004  L6  PUSH_CONST 2 ; 3
  0005  L6  PUSH_CONST 3 ; 4
  0006  L6  CALL 2
  0007  L6  CALL 1
  0008  L6  POP 0
  0009  L1  PUSH_UNIT 0
  0010  L1  RETURN 0

  function area arity=2 locals=2 stack=2 captures=0
    0000  L2  PUSH_LOCAL 0
    0001  L2  PUSH_LOCAL 1
    0002  L2  MUL 0
    0003  L2  STORE_LOCAL 2
    0004  L3  PUSH_LOCAL 2
    0005  L3  PUSH_CONST 0 ; 1
    0006  L3  SUB 0
    0007  L3  STORE_LOCAL 3
    0008  L4  PUSH_LOCAL 3
    0009  L4  PUSH_CONST 0 ; 1
    0010  L4  ADD 0
    0011  L1  RETURN 0
```

- `<top>` 0000-0001: the `def` is compiled first (1.1) and bound to the
  global `area`.
- `area` has `arity=2` (slots 0 and 1) and `locals=2`: `a` is slot 2, `b`
  slot 3. A local `val` is a `STORE_LOCAL` and every read a `PUSH_LOCAL`.
- `stack=2`: the deepest point is two operands of `MUL`, `SUB` or `ADD`.
- The `RETURN` carries line 1, the `def`'s line: the compiler gives the
  closing `RETURN` the position of the function.

### Example 03: `if` and `match`

<!-- vm-example: 03-conditionals -->
```scala
def sign(n: Int): String = if n < 0 then "negative" else "non-negative"

def name(n: Int): String = n match
  case 0 => "zero"
  case 1 | 2 => "small"
  case k if k > 100 => "big"
  case _ => "other"

println(sign(-5))
println(name(2))
```

```text
function <top> arity=0 locals=0 stack=3 captures=0
  0000  L1  MAKE_FN 0 ; -> block 0
  0001  L1  STORE_GLOBAL 0 ; sign
  0002  L3  MAKE_FN 1 ; -> block 1
  0003  L3  STORE_GLOBAL 1 ; name
  0004  L9  PUSH_GLOBAL 2 ; println
  0005  L9  PUSH_GLOBAL 0 ; sign
  0006  L9  PUSH_CONST 3 ; -5
  0007  L9  CALL 1
  0008  L9  CALL 1
  0009  L9  POP 0
  0010  L10  PUSH_GLOBAL 2 ; println
  0011  L10  PUSH_GLOBAL 1 ; name
  0012  L10  PUSH_CONST 4 ; 2
  0013  L10  CALL 1
  0014  L10  CALL 1
  0015  L10  POP 0
  0016  L1  PUSH_UNIT 0
  0017  L1  RETURN 0

  function sign arity=1 locals=0 stack=2 captures=0
    0000  L1  PUSH_LOCAL 0
    0001  L1  PUSH_CONST 0 ; 0
    0002  L1  LT 0
    0003  L1  JUMP_IF_FALSE 2 ; -> 6
    0004  L1  PUSH_CONST 1 ; "negative"
    0005  L1  JUMP 1 ; -> 7
    0006  L1  PUSH_CONST 2 ; "non-negative"
    0007  L1  RETURN 0

  function name arity=1 locals=2 stack=2 captures=0
    0000  L3  PUSH_LOCAL 0
    0001  L3  STORE_LOCAL 1
    0002  L4  PUSH_CONST 0 ; 0
    0003  L4  PUSH_LOCAL 1
    0004  L4  EQ 0
    0005  L4  JUMP_IF_FALSE 2 ; -> 8
    0006  L4  PUSH_CONST 1 ; "zero"
    0007  L4  JUMP 23 ; -> 31
    0008  L5  PUSH_CONST 2 ; 1
    0009  L5  PUSH_LOCAL 1
    0010  L5  EQ 0
    0011  L5  JUMP_IF_FALSE 1 ; -> 13
    0012  L5  JUMP 4 ; -> 17
    0013  L5  PUSH_CONST 3 ; 2
    0014  L5  PUSH_LOCAL 1
    0015  L5  EQ 0
    0016  L5  JUMP_IF_FALSE 2 ; -> 19
    0017  L5  PUSH_CONST 4 ; "small"
    0018  L5  JUMP 12 ; -> 31
    0019  L6  PUSH_LOCAL 1
    0020  L6  STORE_LOCAL 2
    0021  L6  PUSH_LOCAL 2
    0022  L6  PUSH_CONST 5 ; 100
    0023  L6  GT 0
    0024  L6  JUMP_IF_FALSE 2 ; -> 27
    0025  L6  PUSH_CONST 6 ; "big"
    0026  L6  JUMP 4 ; -> 31
    0027  L7  PUSH_CONST 7 ; "other"
    0028  L7  JUMP 2 ; -> 31
    0029  L3  PUSH_LOCAL 1
    0030  L3  MATCH_ERROR 0
    0031  L3  RETURN 0
```

- `sign`: `if` is `JUMP_IF_FALSE` to the else branch and `JUMP` over it; both
  branches leave one value at the join (0007).
- `name` 0000-0001: the scrutinee is copied into a fresh slot (slot 1), which
  every case reads.
- A literal pattern is `literal == scrutinee` (`PUSH_CONST`, `PUSH_LOCAL`,
  `EQ`, `JUMP_IF_FALSE` to the next case), 0002-0005.
- `1 | 2` (0008-0016): a matching first alternative jumps to the body
  (`JUMP 4` at 0012, to 0017); the last alternative falls into it.
- `case k if k > 100` (0019-0024): the binding `k` is a copy of the scrutinee
  into slot 2; the guard is one more `JUMP_IF_FALSE` to the next case.
- 0029-0030: after the last case, `MATCH_ERROR` with the scrutinee. Here the
  `case _` makes it unreachable, but it is always emitted.

### Example 04: `def` and a call of it

<!-- vm-example: 04-def-call -->
```scala
def square(x: Int): Int = x * x

println(square(7))
```

```text
function <top> arity=0 locals=0 stack=3 captures=0
  0000  L1  MAKE_FN 0 ; -> block 0
  0001  L1  STORE_GLOBAL 0 ; square
  0002  L3  PUSH_GLOBAL 1 ; println
  0003  L3  PUSH_GLOBAL 0 ; square
  0004  L3  PUSH_CONST 2 ; 7
  0005  L3  CALL 1
  0006  L3  CALL 1
  0007  L3  POP 0
  0008  L1  PUSH_UNIT 0
  0009  L1  RETURN 0

  function square arity=1 locals=0 stack=2 captures=0
    0000  L1  PUSH_LOCAL 0
    0001  L1  PUSH_LOCAL 0
    0002  L1  MUL 0
    0003  L1  RETURN 0
```

- A call of a global function is `PUSH_GLOBAL` of the callee, the arguments,
  then `CALL n` (0003-0005). The callee runs in a new frame (1.3).
- The result of `square(7)` stays on the stack as the argument of `println`.

### Example 05: recursion, `fib`

<!-- vm-example: 05-fib -->
```scala
def fib(n: Int): Int =
  if n < 2 then n else fib(n - 1) + fib(n - 2)

println(fib(20))
```

```text
function <top> arity=0 locals=0 stack=3 captures=0
  0000  L1  MAKE_FN 0 ; -> block 0
  0001  L1  STORE_GLOBAL 0 ; fib
  0002  L4  PUSH_GLOBAL 1 ; println
  0003  L4  PUSH_GLOBAL 0 ; fib
  0004  L4  PUSH_CONST 2 ; 20
  0005  L4  CALL 1
  0006  L4  CALL 1
  0007  L4  POP 0
  0008  L1  PUSH_UNIT 0
  0009  L1  RETURN 0

  function fib arity=1 locals=0 stack=4 captures=0
    0000  L2  PUSH_LOCAL 0
    0001  L2  PUSH_CONST 0 ; 2
    0002  L2  LT 0
    0003  L2  JUMP_IF_FALSE 2 ; -> 6
    0004  L2  PUSH_LOCAL 0
    0005  L2  JUMP 11 ; -> 17
    0006  L2  PUSH_GLOBAL 1 ; fib
    0007  L2  PUSH_LOCAL 0
    0008  L2  PUSH_CONST 2 ; 1
    0009  L2  SUB 0
    0010  L2  CALL 1
    0011  L2  PUSH_GLOBAL 1 ; fib
    0012  L2  PUSH_LOCAL 0
    0013  L2  PUSH_CONST 0 ; 2
    0014  L2  SUB 0
    0015  L2  CALL 1
    0016  L2  ADD 0
    0017  L1  RETURN 0
```

- Inside `fib`, the recursive call reads the global `fib` (0006, 0011), so a
  REPL redefinition of another name never affects it, and each call is a full
  `CALL` that nests on the native stack.
- `n < 2`, `n - 1`, `n - 2` and the final `+` are the fast-path opcodes
  (`LT`, `SUB`, `ADD`); on SmallIntegers none of them leaves the dispatch
  loop.
- `stack=4`: at 0013 the stack holds the first call's result, `fib`, `n` and
  `2`. The header makes the frame size visible.

### Example 06: loops, `while` and a tail-recursive `def`

<!-- vm-example: 06-loop -->
```scala
def sumTo(n: Int): Int =
  var i = 0
  var acc = 0
  while i <= n do
    acc = acc + i
    i = i + 1
  acc

@annotation.tailrec
def count(n: Int, acc: Int): Int =
  if n == 0 then acc else count(n - 1, acc + 1)

println(sumTo(100))
println(count(1000000, 0))
```

```text
function <top> arity=0 locals=0 stack=4 captures=0
  0000  L1  MAKE_FN 0 ; -> block 0
  0001  L1  STORE_GLOBAL 0 ; sumTo
  0002  L10  MAKE_FN 1 ; -> block 1
  0003  L10  STORE_GLOBAL 1 ; count
  0004  L13  PUSH_GLOBAL 2 ; println
  0005  L13  PUSH_GLOBAL 0 ; sumTo
  0006  L13  PUSH_CONST 3 ; 100
  0007  L13  CALL 1
  0008  L13  CALL 1
  0009  L13  POP 0
  0010  L14  PUSH_GLOBAL 2 ; println
  0011  L14  PUSH_GLOBAL 1 ; count
  0012  L14  PUSH_CONST 4 ; 1000000
  0013  L14  PUSH_CONST 5 ; 0
  0014  L14  CALL 2
  0015  L14  CALL 1
  0016  L14  POP 0
  0017  L1  PUSH_UNIT 0
  0018  L1  RETURN 0

  function sumTo arity=1 locals=2 stack=2 captures=0
    0000  L2  PUSH_CONST 0 ; 0
    0001  L2  STORE_LOCAL 1
    0002  L3  PUSH_CONST 0 ; 0
    0003  L3  STORE_LOCAL 2
    0004  L4  PUSH_LOCAL 1
    0005  L4  PUSH_LOCAL 0
    0006  L4  LE 0
    0007  L4  JUMP_IF_FALSE 13 ; -> 21
    0008  L5  PUSH_LOCAL 2
    0009  L5  PUSH_LOCAL 1
    0010  L5  ADD 0
    0011  L5  STORE_LOCAL 2
    0012  L5  PUSH_UNIT 0
    0013  L5  POP 0
    0014  L6  PUSH_LOCAL 1
    0015  L6  PUSH_CONST 1 ; 1
    0016  L6  ADD 0
    0017  L6  STORE_LOCAL 1
    0018  L6  PUSH_UNIT 0
    0019  L4  POP 0
    0020  L4  JUMP_BACK 17 ; -> 4
    0021  L4  PUSH_UNIT 0
    0022  L4  POP 0
    0023  L7  PUSH_LOCAL 2
    0024  L1  RETURN 0

  function count arity=2 locals=0 stack=3 captures=0
    0000  L11  PUSH_LOCAL 0
    0001  L11  PUSH_CONST 0 ; 0
    0002  L11  EQ 0
    0003  L11  JUMP_IF_FALSE 2 ; -> 6
    0004  L11  PUSH_LOCAL 1
    0005  L11  JUMP 9 ; -> 15
    0006  L11  PUSH_LOCAL 0
    0007  L11  PUSH_CONST 1 ; 1
    0008  L11  SUB 0
    0009  L11  PUSH_LOCAL 1
    0010  L11  PUSH_CONST 1 ; 1
    0011  L11  ADD 0
    0012  L11  STORE_LOCAL 1
    0013  L11  STORE_LOCAL 0
    0014  L11  JUMP_BACK 15 ; -> 0
    0015  L10  RETURN 0
```

- `sumTo`: the loop condition is at 0004; `JUMP_IF_FALSE 13` exits to 0021.
  The body's value is dropped (0019) and `JUMP_BACK 17` returns to 0004,
  polling the GC safepoint (1.6). The `while` itself yields Unit (0021),
  dropped at 0022 because it is a statement.
- An assignment yields Unit (0012, 0018), which the enclosing block drops.
- `count` is annotated `@annotation.tailrec`, so its self-call in tail
  position is a loop (`src/compiler/CompileTailRec.cpp`): the new arguments
  are evaluated onto the stack (0006-0011), stored into the parameter slots in
  reverse (0012-0013), and `JUMP_BACK 15` returns to word 0, polling the GC
  safepoint as the `while` loop does. No `CALL` and no `PUSH_GLOBAL count`
  remain, the frame and its context are reused, and `count(1000000, 0)` needs
  no native stack. Word 0 is the loop head on purpose: it is where the
  prologue widens a `Double` parameter, so a rebound one is widened again.
  The `RETURN` at 0015 is reached only from the `then` branch.

### Example 07: closures capturing a `val` and a `var`

<!-- vm-example: 07-closure -->
```scala
def adder(n: Int): Int => Int = (x: Int) => x + n

def counter(): () => Int =
  var c = 0
  () => { c = c + 1; c }

val add5 = adder(5)
println(add5(10))
val next = counter()
next()
println(next())
```

```text
function <top> arity=0 locals=0 stack=3 captures=0
  0000  L1  MAKE_FN 0 ; -> block 0
  0001  L1  STORE_GLOBAL 0 ; adder
  0002  L3  MAKE_FN 1 ; -> block 1
  0003  L3  STORE_GLOBAL 1 ; counter
  0004  L7  PUSH_GLOBAL 0 ; adder
  0005  L7  PUSH_CONST 2 ; 5
  0006  L7  CALL 1
  0007  L7  STORE_GLOBAL 3 ; add5
  0008  L8  PUSH_GLOBAL 4 ; println
  0009  L8  PUSH_GLOBAL 3 ; add5
  0010  L8  PUSH_CONST 5 ; 10
  0011  L8  CALL 1
  0012  L8  CALL 1
  0013  L8  POP 0
  0014  L9  PUSH_GLOBAL 1 ; counter
  0015  L9  CALL 0
  0016  L9  STORE_GLOBAL 6 ; next
  0017  L10  PUSH_GLOBAL 6 ; next
  0018  L10  CALL 0
  0019  L10  POP 0
  0020  L11  PUSH_GLOBAL 4 ; println
  0021  L11  PUSH_GLOBAL 6 ; next
  0022  L11  CALL 0
  0023  L11  CALL 1
  0024  L11  POP 0
  0025  L1  PUSH_UNIT 0
  0026  L1  RETURN 0

  function adder arity=1 locals=0 stack=1 captures=0
    0000  L1  PUSH_LOCAL 0
    0001  L1  MAKE_FN 0 ; -> block 0
    0002  L1  RETURN 0

    function <lambda> arity=1 locals=1 stack=2 captures=1
      captures: slot 1 <- parent slot 0
      0000  L1  PUSH_LOCAL 0
      0001  L1  PUSH_LOCAL 1
      0002  L1  ADD 0
      0003  L1  RETURN 0

  function counter arity=0 locals=1 stack=1 captures=0
    0000  L4  MAKE_CELL 0
    0001  L4  PUSH_CONST 0 ; 0
    0002  L4  STORE_CELL 0
    0003  L5  PUSH_LOCAL 0
    0004  L5  MAKE_FN 0 ; -> block 0
    0005  L3  RETURN 0

    function <lambda> arity=0 locals=1 stack=2 captures=1
      captures: slot 0 <- parent slot 0
      0000  L5  PUSH_CELL 0
      0001  L5  PUSH_CONST 0 ; 1
      0002  L5  ADD 0
      0003  L5  STORE_CELL 0
      0004  L5  PUSH_UNIT 0
      0005  L5  POP 0
      0006  L5  PUSH_CELL 0
      0007  L5  RETURN 0
```

- `adder`: the lambda captures the parameter `n`. The enclosing frame pushes
  it (`PUSH_LOCAL 0`) and `MAKE_FN 0` pops it into the function value's
  `__captures__`. In the lambda, `captures: slot 1 <- parent slot 0`: the
  value lands in slot 1, after the lambda's own parameter `x`.
- `counter`: `c` is a `var` captured by a closure, so it is **boxed**:
  `MAKE_CELL 0` puts a Cell in slot 0 and the initialiser is a `STORE_CELL`.
  The capture copies the Cell (`PUSH_LOCAL 0` reads the slot, not the value),
  so the lambda's `PUSH_CELL 0` / `STORE_CELL 0` update the same variable.
- `add5(10)` is `CALL 1` on a function value read from a global; `next()` is
  `CALL 0`.

### Example 08: default and named arguments, overloads

<!-- vm-example: 08-defaults -->
```scala
def greet(name: String, greeting: String = "Hello"): String = s"$greeting, $name"

def area(w: Int): Int = w * w
def area(w: Int, h: Int): Int = w * h

println(greet("Ada"))
println(greet("Ada", greeting = "Hi"))
println(area(3) + area(3, 4))
```

```text
function <top> arity=0 locals=0 stack=5 captures=0
  0000  L1  MAKE_FN 0 ; -> block 0
  0001  L1  STORE_GLOBAL 0 ; greet
  0002  L3  MAKE_FN 1 ; -> block 1
  0003  L3  STORE_GLOBAL 1 ; area
  0004  L4  MAKE_FN 2 ; -> block 2
  0005  L4  STORE_GLOBAL 2 ; area#1
  0006  L6  PUSH_GLOBAL 3 ; println
  0007  L6  PUSH_GLOBAL 0 ; greet
  0008  L6  PUSH_CONST 4 ; "Ada"
  0009  L6  CALL 1
  0010  L6  CALL 1
  0011  L6  POP 0
  0012  L7  PUSH_GLOBAL 3 ; println
  0013  L7  PUSH_GLOBAL 0 ; greet
  0014  L7  PUSH_CONST 4 ; "Ada"
  0015  L7  PUSH_CONST 5 ; "Hi"
  0016  L7  CALL_KW 6 ; apply/1(greeting=)
  0017  L7  CALL 1
  0018  L7  POP 0
  0019  L8  PUSH_GLOBAL 3 ; println
  0020  L8  PUSH_GLOBAL 1 ; area
  0021  L8  PUSH_CONST 7 ; 3
  0022  L8  CALL 1
  0023  L8  PUSH_GLOBAL 2 ; area#1
  0024  L8  PUSH_CONST 7 ; 3
  0025  L8  PUSH_CONST 8 ; 4
  0026  L8  CALL 2
  0027  L8  ADD 0
  0028  L8  CALL 1
  0029  L8  POP 0
  0030  L1  PUSH_UNIT 0
  0031  L1  RETURN 0

  function greet arity=2 locals=0 stack=3 captures=0
    0000  L1  PUSH_LOCAL 1
    0001  L1  PUSH_CONST 0 ; ", "
    0002  L1  PUSH_LOCAL 0
    0003  L1  CONCAT 3
    0004  L1  RETURN 0

    function greet$default$1 arity=2 locals=0 stack=1 captures=0
      0000  L1  PUSH_CONST 0 ; "Hello"
      0001  L1  RETURN 0

  function area arity=1 locals=0 stack=2 captures=0
    0000  L3  PUSH_LOCAL 0
    0001  L3  PUSH_LOCAL 0
    0002  L3  MUL 0
    0003  L3  RETURN 0

  function area arity=2 locals=0 stack=2 captures=0
    0000  L4  PUSH_LOCAL 0
    0001  L4  PUSH_LOCAL 1
    0002  L4  MUL 0
    0003  L4  RETURN 0
```

- `greet("Ada")` (0007-0009) is a plain `CALL 1`: the caller does not know the
  default. The callee's prologue runs `greet$default$1` for the missing
  parameter (1.5). That block mirrors `greet`'s slot prefix (`arity=2`), so a
  default could read `name`.
- `greet("Ada", greeting = "Hi")` is `CALL_KW` (0016): the site
  `apply/1(greeting=)` says one positional argument and one named one; the
  callee binds `greeting` by name.
- `greet`'s body is `s"$greeting, $name"`: one `CONCAT 3`.
- The two `area` overloads are two globals, `area` and `area#1`; the call
  site picks one by its argument count at compile time (D111), 0020 and 0023.

### Example 09: case classes, patterns and tuple destructuring

<!-- vm-example: 09-destructuring -->
```scala
case class Point(x: Int, y: Int)

def describe(p: Point): String = p match
  case Point(0, 0) => "origin"
  case Point(x, y) => s"($x, $y)"

val (a, b) = (1, 2)
println(describe(Point(a, b)))
```

```text
function <top> arity=0 locals=6 stack=5 captures=0
  0000  L1  PUSH_GLOBAL 0 ; @Product
  0001  L1  PUSH_GLOBAL 1 ; @AnyRef
  0002  L1  PUSH_GLOBAL 2 ; @Any
  0003  L1  MAKE_FN 0 ; -> block 0
  0004  L1  MAKE_CLASS 3 ; class Point @Point parents=3 members=[<init>] fields=[x,y]
  0005  L1  STORE_GLOBAL 4 ; @Point
  0006  L1  PUSH_GLOBAL 1 ; @AnyRef
  0007  L1  PUSH_GLOBAL 2 ; @Any
  0008  L1  MAKE_FN 1 ; -> block 1
  0009  L1  MAKE_FN 2 ; -> block 2
  0010  L1  MAKE_FN 3 ; -> block 3
  0011  L1  MAKE_CLASS 5 ; class Point @Point.type parents=2 members=[apply,unapply,<init>]
  0012  L1  STORE_GLOBAL 6 ; @Point.type
  0013  L1  MAKE_FN 4 ; -> block 4
  0014  L1  MAKE_LAZY 0
  0015  L1  STORE_GLOBAL 7 ; Point
  0016  L3  MAKE_FN 5 ; -> block 5
  0017  L3  STORE_GLOBAL 8 ; describe
  0018  L7  PUSH_CONST 9 ; 1
  0019  L7  PUSH_CONST 10 ; 2
  0020  L7  MAKE_TUPLE 2
  0021  L7  STORE_LOCAL 1
  0022  L7  PUSH_LOCAL 1
  0023  L7  TEST_PROTO 11 ; @Tuple2
  0024  L7  JUMP_IF_FALSE 12 ; -> 37
  0025  L7  PUSH_LOCAL 1
  0026  L7  UNAPPLY_FIELDS 12 ; [_1,_2]
  0027  L7  STORE_LOCAL 2
  0028  L7  STORE_LOCAL 3
  0029  L7  PUSH_LOCAL 3
  0030  L7  STORE_LOCAL 4
  0031  L7  PUSH_LOCAL 2
  0032  L7  STORE_LOCAL 5
  0033  L7  PUSH_LOCAL 4
  0034  L7  PUSH_LOCAL 5
  0035  L7  MAKE_TUPLE 2
  0036  L7  JUMP 2 ; -> 39
  0037  L7  PUSH_LOCAL 1
  0038  L7  MATCH_ERROR 0
  0039  L7  STORE_LOCAL 0
  0040  L7  PUSH_LOCAL 0
  0041  L7  SEND 13 ; _1/0
  0042  L7  STORE_GLOBAL 14 ; a
  0043  L7  PUSH_LOCAL 0
  0044  L7  SEND 15 ; _2/0
  0045  L7  STORE_GLOBAL 16 ; b
  0046  L8  PUSH_GLOBAL 17 ; println
  0047  L8  PUSH_GLOBAL 8 ; describe
  0048  L8  PUSH_GLOBAL 4 ; @Point
  0049  L8  PUSH_GLOBAL 14 ; a
  0050  L8  PUSH_GLOBAL 16 ; b
  0051  L8  NEW 18 ; <init>/2
  0052  L8  CALL 1
  0053  L8  CALL 1
  0054  L8  POP 0
  0055  L1  PUSH_UNIT 0
  0056  L1  RETURN 0

  function Point.<init> arity=3 method locals=0 stack=2 captures=0
    0000  L1  PUSH_LOCAL 1
    0001  L1  PUSH_LOCAL 2
    0002  L1  STORE_FIELDS_IF_NEW 0 ; [x,y]
    0003  L1  PUSH_LOCAL 0
    0004  L1  RETURN 0

  function apply arity=3 method locals=0 stack=3 captures=0
    0000  L1  PUSH_GLOBAL 0 ; @Point
    0001  L1  PUSH_LOCAL 1
    0002  L1  PUSH_LOCAL 2
    0003  L1  NEW 1 ; <init>/2
    0004  L1  RETURN 0

  function unapply arity=2 method locals=0 stack=1 captures=0
    0000  L1  PUSH_LOCAL 1
    0001  L1  RETURN 0

  function Point.<init> arity=1 method locals=0 stack=1 captures=0
    0000  L1  PUSH_LOCAL 0
    0001  L1  RETURN 0

  function <object Point> arity=0 locals=0 stack=1 captures=0
    0000  L1  PUSH_GLOBAL 0 ; @Point.type
    0001  L1  NEW 1 ; <init>/0
    0002  L1  RETURN 0

  function describe arity=1 locals=7 stack=5 captures=0
    0000  L3  PUSH_LOCAL 0
    0001  L3  STORE_LOCAL 1
    0002  L4  PUSH_LOCAL 1
    0003  L4  TEST_PROTO 0 ; @Point
    0004  L4  JUMP_IF_FALSE 14 ; -> 19
    0005  L4  PUSH_LOCAL 1
    0006  L4  UNAPPLY_FIELDS 1 ; [x,y]
    0007  L4  STORE_LOCAL 2
    0008  L4  STORE_LOCAL 3
    0009  L4  PUSH_CONST 2 ; 0
    0010  L4  PUSH_LOCAL 3
    0011  L4  EQ 0
    0012  L4  JUMP_IF_FALSE 6 ; -> 19
    0013  L4  PUSH_CONST 2 ; 0
    0014  L4  PUSH_LOCAL 2
    0015  L4  EQ 0
    0016  L4  JUMP_IF_FALSE 2 ; -> 19
    0017  L4  PUSH_CONST 3 ; "origin"
    0018  L4  JUMP 20 ; -> 39
    0019  L5  PUSH_LOCAL 1
    0020  L5  TEST_PROTO 0 ; @Point
    0021  L5  JUMP_IF_FALSE 15 ; -> 37
    0022  L5  PUSH_LOCAL 1
    0023  L5  UNAPPLY_FIELDS 1 ; [x,y]
    0024  L5  STORE_LOCAL 4
    0025  L5  STORE_LOCAL 5
    0026  L5  PUSH_LOCAL 5
    0027  L5  STORE_LOCAL 6
    0028  L5  PUSH_LOCAL 4
    0029  L5  STORE_LOCAL 7
    0030  L5  PUSH_CONST 4 ; "("
    0031  L5  PUSH_LOCAL 6
    0032  L5  PUSH_CONST 5 ; ", "
    0033  L5  PUSH_LOCAL 7
    0034  L5  PUSH_CONST 6 ; ")"
    0035  L5  CONCAT 5
    0036  L5  JUMP 2 ; -> 39
    0037  L3  PUSH_LOCAL 1
    0038  L3  MATCH_ERROR 0
    0039  L3  RETURN 0
```

- `<top>` 0000-0005: the class `Point`: its parents (`@Product`, `@AnyRef`,
  `@Any`, the linearization), its one member (the constructor, block 0),
  `MAKE_CLASS`, bound to the type key `@Point`. The `ClassSpec` comment lists
  the product fields `[x,y]`.
- 0006-0015: the companion object: its class `@Point.type` with the
  synthesised `apply` and `unapply`, and the lazy holder `Point`
  (`MAKE_FN 4`, `MAKE_LAZY`).
- `Point.<init>` stores both parameters as one field group
  (`STORE_FIELDS_IF_NEW`) and returns `this` (slot 0).
- `describe`: `case Point(0, 0)` is `TEST_PROTO @Point` (class membership by
  marker), `UNAPPLY_FIELDS [x,y]` into two slots (stored in reverse order,
  `y` first), then a literal test per field. `case Point(x, y)` repeats the
  test and binds the fields; the body is a `CONCAT`.
- `val (a, b) = (1, 2)` at the top level (0018-0045) is a `match` on the
  tuple (`MAKE_TUPLE 2`, `TEST_PROTO @Tuple2`, `UNAPPLY_FIELDS [_1,_2]`)
  whose result is a new tuple of the bound values, stored in slot 0 of the
  top-level frame (0039); `a` and `b` are then that tuple's `_1` and `_2`
  (0040-0045). The temporary is a slot, not a global, so no hidden name is
  declared and the tuple dies with the frame (section 6).
- `Point(a, b)` compiles straight to `NEW <init>/2` on `@Point` (0048-0051):
  the synthesised `apply` is bypassed.

### Example 10: keyed lookup, `m("k")` and `m.get`

<!-- vm-example: 10-keyed-lookup -->
```scala
val ages = Map("ada" -> 36, "alan" -> 41)
println(ages("ada"))
println(ages.get("grace"))
```

```text
function <top> arity=0 locals=0 stack=4 captures=0
  0000  L1  PUSH_GLOBAL 0 ; Map
  0001  L1  PUSH_CONST 1 ; "ada"
  0002  L1  PUSH_CONST 2 ; 36
  0003  L1  SEND_APPLY 3 ; ->/1
  0004  L1  PUSH_CONST 4 ; "alan"
  0005  L1  PUSH_CONST 5 ; 41
  0006  L1  SEND_APPLY 3 ; ->/1
  0007  L1  CALL 2
  0008  L1  STORE_GLOBAL 6 ; ages
  0009  L2  PUSH_GLOBAL 7 ; println
  0010  L2  PUSH_GLOBAL 6 ; ages
  0011  L2  PUSH_CONST 1 ; "ada"
  0012  L2  CALL 1
  0013  L2  CALL 1
  0014  L2  POP 0
  0015  L3  PUSH_GLOBAL 7 ; println
  0016  L3  PUSH_GLOBAL 6 ; ages
  0017  L3  PUSH_CONST 8 ; "grace"
  0018  L3  SEND_APPLY 9 ; get/1
  0019  L3  CALL 1
  0020  L3  POP 0
  0021  L1  PUSH_UNIT 0
  0022  L1  RETURN 0
```

- `Map(...)` is `PUSH_GLOBAL Map` and `CALL 2`; each `"ada" -> 36` is a
  `SEND_APPLY ->/1` on the key.
- `ages("ada")` is `CALL 1` on the map: a map is not a function value, so
  `invoke` sends it `apply` (universal apply, 1.5).
- `ages.get("grace")` is `SEND_APPLY get/1`, answered by a native method.

### Example 11: built-in methods, `xs.head`, `xs.length`, `4.max(9)`

<!-- vm-example: 11-primitive-call -->
```scala
val xs = List(3, 1, 2)
println(xs.head)
println(xs.length)
println(4.max(9))
```

```text
function <top> arity=0 locals=0 stack=4 captures=0
  0000  L1  PUSH_GLOBAL 0 ; List
  0001  L1  PUSH_CONST 1 ; 3
  0002  L1  PUSH_CONST 2 ; 1
  0003  L1  PUSH_CONST 3 ; 2
  0004  L1  CALL 3
  0005  L1  STORE_GLOBAL 4 ; xs
  0006  L2  PUSH_GLOBAL 5 ; println
  0007  L2  PUSH_GLOBAL 4 ; xs
  0008  L2  SEND 6 ; head/0
  0009  L2  CALL 1
  0010  L2  POP 0
  0011  L3  PUSH_GLOBAL 5 ; println
  0012  L3  PUSH_GLOBAL 4 ; xs
  0013  L3  SEND 7 ; length/0
  0014  L3  CALL 1
  0015  L3  POP 0
  0016  L4  PUSH_GLOBAL 5 ; println
  0017  L4  PUSH_CONST 8 ; 4
  0018  L4  PUSH_CONST 9 ; 9
  0019  L4  SEND_APPLY 10 ; max/1
  0020  L4  CALL 1
  0021  L4  POP 0
  0022  L1  PUSH_UNIT 0
  0023  L1  RETURN 0
```

- `xs.head` and `xs.length` have no argument list: `SEND head/0`,
  `SEND length/0`. The members are native methods on the List prototype;
  each call goes through `callNative`, which creates a scope context
  (section 5).
- `4.max(9)` is `SEND_APPLY max/1` on an integer: SmallIntegers have a
  prototype like every other value.
- `List(3, 1, 2)` is a call of the List companion (`CALL 3`).

### Example 12: a higher-order call, `foldLeft` with a lambda

<!-- vm-example: 12-fold -->
```scala
val total = List(1, 2, 3, 4).foldLeft(0)((acc, x) => acc + x)
println(total)
```

```text
function <top> arity=0 locals=0 stack=5 captures=0
  0000  L1  PUSH_GLOBAL 0 ; List
  0001  L1  PUSH_CONST 1 ; 1
  0002  L1  PUSH_CONST 2 ; 2
  0003  L1  PUSH_CONST 3 ; 3
  0004  L1  PUSH_CONST 4 ; 4
  0005  L1  CALL 4
  0006  L1  PUSH_CONST 5 ; 0
  0007  L1  SEND_APPLY 6 ; foldLeft/1
  0008  L1  MAKE_FN 0 ; -> block 0
  0009  L1  CALL 1
  0010  L1  STORE_GLOBAL 7 ; total
  0011  L2  PUSH_GLOBAL 8 ; println
  0012  L2  PUSH_GLOBAL 7 ; total
  0013  L2  CALL 1
  0014  L2  POP 0
  0015  L1  PUSH_UNIT 0
  0016  L1  RETURN 0

  function <lambda> arity=2 locals=0 stack=2 captures=0
    0000  L1  PUSH_LOCAL 0
    0001  L1  PUSH_LOCAL 1
    0002  L1  ADD 0
    0003  L1  RETURN 0
```

- `foldLeft(0)(f)` has two argument lists: `SEND_APPLY foldLeft/1` calls the
  native `foldLeft` with one argument, which answers a one-shot applier object
  (`makeFoldPartial` in `CollectionPrimitives.cpp`); `CALL 1` on that object
  is a universal `apply` (1.5), a second native call that runs the fold.
- The lambda captures nothing; its two parameters are slots 0 and 1. The
  fold calls it back through `ExecutionEngine::invoke`, a new frame per
  element.

### Example 13: `try` / `catch` / `finally` / `throw`

<!-- vm-example: 13-try-catch -->
```scala
def safeDiv(a: Int, b: Int): Int =
  try
    if b == 0 then throw new ArithmeticException("divide by zero")
    a / b
  catch
    case e: ArithmeticException => -1
  finally
    println("done")

println(safeDiv(6, 3))
println(safeDiv(1, 0))
```

```text
function <top> arity=0 locals=0 stack=4 captures=0
  0000  L1  MAKE_FN 0 ; -> block 0
  0001  L1  STORE_GLOBAL 0 ; safeDiv
  0002  L10  PUSH_GLOBAL 1 ; println
  0003  L10  PUSH_GLOBAL 0 ; safeDiv
  0004  L10  PUSH_CONST 2 ; 6
  0005  L10  PUSH_CONST 3 ; 3
  0006  L10  CALL 2
  0007  L10  CALL 1
  0008  L10  POP 0
  0009  L11  PUSH_GLOBAL 1 ; println
  0010  L11  PUSH_GLOBAL 0 ; safeDiv
  0011  L11  PUSH_CONST 4 ; 1
  0012  L11  PUSH_CONST 5 ; 0
  0013  L11  CALL 2
  0014  L11  CALL 1
  0015  L11  POP 0
  0016  L1  PUSH_UNIT 0
  0017  L1  RETURN 0

  function safeDiv arity=2 locals=2 stack=3 captures=0
    0000  L3  PUSH_LOCAL 1
    0001  L3  PUSH_CONST 0 ; 0
    0002  L3  EQ 0
    0003  L3  JUMP_IF_FALSE 8 ; -> 12
    0004  L3  PUSH_GLOBAL 1 ; @ArithmeticException
    0005  L3  PUSH_CONST 2 ; "divide by zero"
    0006  L3  NEW 3 ; <init>/1
    0007  L3  THROW 0
    0008  L3  PUSH_UNIT 0
    0009  L3  POP 0
    0010  L3  PUSH_UNIT 0
    0011  L3  JUMP 1 ; -> 13
    0012  L3  PUSH_UNIT 0
    0013  L3  POP 0
    0014  L4  PUSH_LOCAL 0
    0015  L4  PUSH_LOCAL 1
    0016  L4  SEND_APPLY 4 ; //1
    0017  L2  JUMP 8 ; -> 26
    0018  L6  PUSH_LOCAL 2
    0019  L6  TEST_PROTO 1 ; @ArithmeticException
    0020  L6  JUMP_IF_FALSE 4 ; -> 25
    0021  L6  PUSH_LOCAL 2
    0022  L6  STORE_LOCAL 3
    0023  L6  PUSH_CONST 5 ; -1
    0024  L6  JUMP 1 ; -> 26
    0025  L2  RETHROW 2
    0026  L2  JUMP 5 ; -> 32
    0027  L8  PUSH_GLOBAL 6 ; println
    0028  L8  PUSH_CONST 7 ; "done"
    0029  L8  CALL 1
    0030  L2  POP 0
    0031  L2  RETHROW 2
    0032  L8  PUSH_GLOBAL 6 ; println
    0033  L8  PUSH_CONST 7 ; "done"
    0034  L8  CALL 1
    0035  L2  POP 0
    0036  L1  RETURN 0
    handler [0, 17) -> 18  depth=0 slot=2 catch
    handler [0, 26) -> 27  depth=0 slot=2 finally
```

- The handler table at the end: `[0, 17) -> 18 ... slot=2 catch` covers the
  body; `[0, 26) -> 27 ... slot=2 finally` covers the body and the catch
  cascade. Slot 2 is the compiler's slot for the caught value; `e` is slot 3.
- 0004-0007: `throw new ArithmeticException(...)` is `NEW` and `THROW`;
  0008 is the unreachable Unit that keeps the stack accounting balanced.
- 0016: `a / b` has no fast opcode: `SEND_APPLY //1` (the method named `/`).
- 0018-0025: the catch cascade reads slot 2, tests the class
  (`TEST_PROTO @ArithmeticException`) and, when nothing matches, `RETHROW 2`
  (0025), which the finally entry still covers.
- 0027-0031: the finally cleanup (the exception path) ends in `RETHROW 2`;
  0032-0035: the same `finally` body on the normal path. The `try`'s value is
  the one left on the stack at 0026.

### Example 14: desugaring, a `for` comprehension

<!-- vm-example: 14-for-comprehension -->
```scala
val pairs = for
  x <- List(1, 2, 3)
  if x != 2
  y <- List(10, 20)
yield x * y

println(pairs)
```

```text
function <top> arity=0 locals=0 stack=4 captures=0
  0000  L2  PUSH_GLOBAL 0 ; List
  0001  L2  PUSH_CONST 1 ; 1
  0002  L2  PUSH_CONST 2 ; 2
  0003  L2  PUSH_CONST 3 ; 3
  0004  L2  CALL 3
  0005  L3  MAKE_FN 0 ; -> block 0
  0006  L3  SEND_APPLY 4 ; withFilter/1
  0007  L2  MAKE_FN 1 ; -> block 1
  0008  L2  SEND_APPLY 5 ; flatMap/1
  0009  L1  STORE_GLOBAL 6 ; pairs
  0010  L7  PUSH_GLOBAL 7 ; println
  0011  L7  PUSH_GLOBAL 6 ; pairs
  0012  L7  CALL 1
  0013  L7  POP 0
  0014  L1  PUSH_UNIT 0
  0015  L1  RETURN 0

  function <lambda> arity=1 locals=0 stack=2 captures=0
    0000  L3  PUSH_LOCAL 0
    0001  L3  PUSH_CONST 0 ; 2
    0002  L3  NE 0
    0003  L3  RETURN 0

  function <lambda> arity=1 locals=0 stack=3 captures=0
    0000  L4  PUSH_GLOBAL 0 ; List
    0001  L4  PUSH_CONST 1 ; 10
    0002  L4  PUSH_CONST 2 ; 20
    0003  L4  CALL 2
    0004  L4  PUSH_LOCAL 0
    0005  L4  MAKE_FN 0 ; -> block 0
    0006  L4  SEND_APPLY 3 ; map/1
    0007  L2  RETURN 0

    function <lambda> arity=1 locals=1 stack=2 captures=1
      captures: slot 1 <- parent slot 0
      0000  L5  PUSH_LOCAL 1
      0001  L5  PUSH_LOCAL 0
      0002  L5  MUL 0
      0003  L4  RETURN 0
```

- The desugarer rewrote the comprehension before compilation:
  `List(1,2,3).withFilter(x => x != 2).flatMap(x => List(10,20).map(y => x * y))`.
- The guard and each generator are lambdas (blocks 0 and 1); the inner `map`
  lambda captures `x` (`captures: slot 1 <- parent slot 0`).
- Nothing here is a new opcode: a comprehension is calls of the collection's
  native `withFilter`, `flatMap` and `map`.

### Example 15: laziness, `lazy val` and a by-name parameter

<!-- vm-example: 15-lazy -->
```scala
lazy val expensive: Int =
  println("computing")
  42

def twice(x: => Int): Int = x + x

println(expensive + expensive)
println(twice { println("evaluated"); 1 })
```

```text
function <top> arity=0 locals=0 stack=3 captures=0
  0000  L1  MAKE_FN 0 ; -> block 0
  0001  L1  MAKE_LAZY 0
  0002  L1  STORE_GLOBAL 0 ; expensive
  0003  L5  MAKE_FN 1 ; -> block 1
  0004  L5  STORE_GLOBAL 1 ; twice
  0005  L7  PUSH_GLOBAL 2 ; println
  0006  L7  PUSH_GLOBAL 0 ; expensive
  0007  L7  FORCE 0
  0008  L7  PUSH_GLOBAL 0 ; expensive
  0009  L7  FORCE 0
  0010  L7  ADD 0
  0011  L7  CALL 1
  0012  L7  POP 0
  0013  L8  PUSH_GLOBAL 2 ; println
  0014  L8  PUSH_GLOBAL 1 ; twice
  0015  L8  MAKE_FN 2 ; -> block 2
  0016  L8  CALL 1
  0017  L8  CALL 1
  0018  L8  POP 0
  0019  L1  PUSH_UNIT 0
  0020  L1  RETURN 0

  function <lazy> arity=0 locals=0 stack=2 captures=0
    0000  L2  PUSH_GLOBAL 0 ; println
    0001  L2  PUSH_CONST 1 ; "computing"
    0002  L2  CALL 1
    0003  L2  POP 0
    0004  L3  PUSH_CONST 2 ; 42
    0005  L1  RETURN 0

  function twice arity=1 locals=0 stack=2 captures=0
    0000  L5  PUSH_LOCAL 0
    0001  L5  FORCE_THUNK 0
    0002  L5  PUSH_LOCAL 0
    0003  L5  FORCE_THUNK 0
    0004  L5  ADD 0
    0005  L5  RETURN 0

  function <by-name> arity=0 locals=0 stack=2 captures=0
    0000  L8  PUSH_GLOBAL 0 ; println
    0001  L8  PUSH_CONST 1 ; "evaluated"
    0002  L8  CALL 1
    0003  L8  POP 0
    0004  L8  PUSH_CONST 2 ; 1
    0005  L8  RETURN 0
```

- `lazy val expensive` (0000-0002): its right-hand side is a thunk
  (`<lazy>`), wrapped by `MAKE_LAZY` and bound to the global. Each read is
  `PUSH_GLOBAL` + `FORCE` (0006-0009); the first `FORCE` runs the thunk, the
  second answers the stored value, so "computing" prints once.
- `twice(x: => Int)`: the call site knows the parameter is by-name and passes
  a zero-argument function (`<by-name>`, `MAKE_FN 2` at 0015). Each read of
  `x` in `twice` is `PUSH_LOCAL 0` + `FORCE_THUNK`, so the argument is
  evaluated twice, as Scala requires.
- Scala's `LazyList` and `view` are not part of protoScala's library; they
  have no example.

### Example 16: shared mutable state, a top-level `var` and a `var` field

<!-- vm-example: 16-mutable-state -->
```scala
var hits = 0

class Counter:
  var count = 0
  def inc(): Unit = count += 1

val c = new Counter()
c.inc()
c.inc()
hits += 1
println(c.count + hits)
```

```text
function <top> arity=0 locals=0 stack=5 captures=0
  0000  L3  PUSH_GLOBAL 0 ; @AnyRef
  0001  L3  PUSH_GLOBAL 1 ; @Any
  0002  L5  MAKE_FN 0 ; -> block 0
  0003  L4  MAKE_FN 1 ; -> block 1
  0004  L3  MAKE_FN 2 ; -> block 2
  0005  L3  MAKE_CLASS 2 ; class Counter @Counter parents=2 members=[inc,count_=,<init>]
  0006  L3  STORE_GLOBAL 3 ; @Counter
  0007  L1  PUSH_CONST 4 ; 0
  0008  L1  STORE_GLOBAL 5 ; hits
  0009  L7  PUSH_GLOBAL 3 ; @Counter
  0010  L7  NEW 6 ; <init>/0
  0011  L7  STORE_GLOBAL 7 ; c
  0012  L8  PUSH_GLOBAL 7 ; c
  0013  L8  SEND_APPLY 8 ; inc/0
  0014  L8  POP 0
  0015  L9  PUSH_GLOBAL 7 ; c
  0016  L9  SEND_APPLY 8 ; inc/0
  0017  L9  POP 0
  0018  L10  PUSH_GLOBAL 5 ; hits
  0019  L10  PUSH_CONST 9 ; 1
  0020  L10  ADD 0
  0021  L10  STORE_GLOBAL 5 ; hits
  0022  L10  PUSH_UNIT 0
  0023  L10  POP 0
  0024  L11  PUSH_GLOBAL 10 ; println
  0025  L11  PUSH_GLOBAL 7 ; c
  0026  L11  SEND 11 ; count/0
  0027  L11  PUSH_GLOBAL 5 ; hits
  0028  L11  ADD 0
  0029  L11  CALL 1
  0030  L11  POP 0
  0031  L1  PUSH_UNIT 0
  0032  L1  RETURN 0

  function inc arity=1 method locals=0 stack=3 captures=0
    0000  L5  PUSH_LOCAL 0
    0001  L5  PUSH_LOCAL 0
    0002  L5  SEND 0 ; count/0
    0003  L5  PUSH_CONST 1 ; 1
    0004  L5  ADD 0
    0005  L5  SEND 2 ; count_=/1
    0006  L5  RETURN 0

  function count_= arity=2 method locals=0 stack=2 captures=0
    0000  L4  PUSH_LOCAL 0
    0001  L4  PUSH_LOCAL 1
    0002  L4  SET_FIELD 0 ; count
    0003  L4  PUSH_UNIT 0
    0004  L4  RETURN 0

  function Counter.<init> arity=1 method locals=0 stack=1 captures=0
    0000  L4  PUSH_CONST 0 ; 0
    0001  L4  STORE_FIELD 1 ; count
    0002  L3  PUSH_LOCAL 0
    0003  L3  RETURN 0
```

- The class `Counter` has a `var`, so its instances are mutable
  (`kClassMutableInstances`) and it gets a synthesised setter `count_=`,
  whose body is `SET_FIELD count`: an in-place write.
- `Counter.<init>` initialises the field with `STORE_FIELD count`.
- `count += 1` inside `inc` desugars to `this.count_=(this.count + 1)`:
  `SEND count/0`, `ADD`, `SEND count_=/1`.
- `hits += 1` on a top-level `var` is `PUSH_GLOBAL`, `ADD`, `STORE_GLOBAL`
  (0018-0021): a global write, visible to every function.
- Actors (the other form of shared state) are library calls with no opcode
  of their own (2.11).

### Example 17: type patterns, `::`, tuples, casts, unary operators

<!-- vm-example: 17-misc -->
```scala
def show(x: Any): String = x match
  case n: Int => s"int $n"
  case h :: t => s"list starting with $h"
  case (a, b) => s"pair of $a and $b"
  case _ => "something else"

println(show(1))
println(show(List(7, 8)))
println(show((1, "one")))
println(1.asInstanceOf[Int] + -(2))
println(!(1 == 2))
```

```text
function <top> arity=0 locals=1 stack=5 captures=0
  0000  L1  MAKE_FN 0 ; -> block 0
  0001  L1  STORE_GLOBAL 0 ; show
  0002  L7  PUSH_GLOBAL 1 ; println
  0003  L7  PUSH_GLOBAL 0 ; show
  0004  L7  PUSH_CONST 2 ; 1
  0005  L7  CALL 1
  0006  L7  CALL 1
  0007  L7  POP 0
  0008  L8  PUSH_GLOBAL 1 ; println
  0009  L8  PUSH_GLOBAL 0 ; show
  0010  L8  PUSH_GLOBAL 3 ; List
  0011  L8  PUSH_CONST 4 ; 7
  0012  L8  PUSH_CONST 5 ; 8
  0013  L8  CALL 2
  0014  L8  CALL 1
  0015  L8  CALL 1
  0016  L8  POP 0
  0017  L9  PUSH_GLOBAL 1 ; println
  0018  L9  PUSH_GLOBAL 0 ; show
  0019  L9  PUSH_CONST 2 ; 1
  0020  L9  PUSH_CONST 6 ; "one"
  0021  L9  MAKE_TUPLE 2
  0022  L9  CALL 1
  0023  L9  CALL 1
  0024  L9  POP 0
  0025  L10  PUSH_GLOBAL 1 ; println
  0026  L10  PUSH_CONST 2 ; 1
  0027  L10  STORE_LOCAL 0
  0028  L10  PUSH_LOCAL 0
  0029  L10  TEST_TYPE 11 ; Null
  0030  L10  JUMP_IF_TRUE 5 ; -> 36
  0031  L10  PUSH_LOCAL 0
  0032  L10  TEST_TYPE 0 ; Int
  0033  L10  JUMP_IF_TRUE 2 ; -> 36
  0034  L10  PUSH_LOCAL 0
  0035  L10  CAST_FAIL 7 ; Int
  0036  L10  PUSH_LOCAL 0
  0037  L10  PUSH_CONST 8 ; 2
  0038  L10  NEG 0
  0039  L10  ADD 0
  0040  L10  CALL 1
  0041  L10  POP 0
  0042  L11  PUSH_GLOBAL 1 ; println
  0043  L11  PUSH_CONST 2 ; 1
  0044  L11  PUSH_CONST 8 ; 2
  0045  L11  EQ 0
  0046  L11  NOT 0
  0047  L11  CALL 1
  0048  L11  POP 0
  0049  L1  PUSH_UNIT 0
  0050  L1  RETURN 0

  function show arity=1 locals=10 stack=4 captures=0
    0000  L1  PUSH_LOCAL 0
    0001  L1  STORE_LOCAL 1
    0002  L2  PUSH_LOCAL 1
    0003  L2  TEST_TYPE 0 ; Int
    0004  L2  JUMP_IF_FALSE 6 ; -> 11
    0005  L2  PUSH_LOCAL 1
    0006  L2  STORE_LOCAL 2
    0007  L2  PUSH_CONST 0 ; "int "
    0008  L2  PUSH_LOCAL 2
    0009  L2  CONCAT 2
    0010  L2  JUMP 36 ; -> 47
    0011  L3  PUSH_LOCAL 1
    0012  L3  TEST_TYPE 7 ; ::
    0013  L3  JUMP_IF_FALSE 12 ; -> 26
    0014  L3  PUSH_LOCAL 1
    0015  L3  UNCONS 0
    0016  L3  STORE_LOCAL 3
    0017  L3  STORE_LOCAL 4
    0018  L3  PUSH_LOCAL 4
    0019  L3  STORE_LOCAL 5
    0020  L3  PUSH_LOCAL 3
    0021  L3  STORE_LOCAL 6
    0022  L3  PUSH_CONST 1 ; "list starting with "
    0023  L3  PUSH_LOCAL 5
    0024  L3  CONCAT 2
    0025  L3  JUMP 21 ; -> 47
    0026  L4  PUSH_LOCAL 1
    0027  L4  TEST_PROTO 2 ; @Tuple2
    0028  L4  JUMP_IF_FALSE 14 ; -> 43
    0029  L4  PUSH_LOCAL 1
    0030  L4  UNAPPLY_FIELDS 3 ; [_1,_2]
    0031  L4  STORE_LOCAL 7
    0032  L4  STORE_LOCAL 8
    0033  L4  PUSH_LOCAL 8
    0034  L4  STORE_LOCAL 9
    0035  L4  PUSH_LOCAL 7
    0036  L4  STORE_LOCAL 10
    0037  L4  PUSH_CONST 4 ; "pair of "
    0038  L4  PUSH_LOCAL 9
    0039  L4  PUSH_CONST 5 ; " and "
    0040  L4  PUSH_LOCAL 10
    0041  L4  CONCAT 4
    0042  L4  JUMP 4 ; -> 47
    0043  L5  PUSH_CONST 6 ; "something else"
    0044  L5  JUMP 2 ; -> 47
    0045  L1  PUSH_LOCAL 1
    0046  L1  MATCH_ERROR 0
    0047  L1  RETURN 0
```

- `case n: Int` is `TEST_TYPE Int`; `case h :: t` is `TEST_TYPE ::` (a
  non-empty list) and `UNCONS`, which pushes the head and the tail (stored
  tail first, 0016-0017); `case (a, b)` is `TEST_PROTO @Tuple2` and
  `UNAPPLY_FIELDS [_1,_2]`.
- Each interpolated string is one `CONCAT`.
- `(1, "one")` is `MAKE_TUPLE 2` (0021).
- `1.asInstanceOf[Int]` (0026-0036): the value goes into a temporary slot of
  `<top>`; `null` passes (`TEST_TYPE Null`, `JUMP_IF_TRUE`), so does an `Int`;
  anything else reaches `CAST_FAIL Int`.
- `-(2)` is `PUSH_CONST 2` and `NEG`; `!(1 == 2)` is `EQ` and `NOT`.

### Example 18: traits, classes, `super`, objects

<!-- vm-example: 18-classes -->
```scala
trait Shape:
  def area: Double
  def describe: String = s"shape with area $area"

class Rect(val w: Double, val h: Double) extends Shape:
  def area: Double = w * h

class Square(side: Double) extends Rect(side, side):
  override def describe: String = "square, " + super.describe

object Unit1:
  val one = 1

println(new Square(2.0).describe)
println(Unit1.one)
```

```text
function <top> arity=0 locals=0 stack=6 captures=0
  0000  L1  PUSH_GLOBAL 0 ; @AnyRef
  0001  L1  PUSH_GLOBAL 1 ; @Any
  0002  L3  MAKE_FN 0 ; -> block 0
  0003  L1  MAKE_CLASS 2 ; class Shape @Shape parents=2 members=[describe]
  0004  L1  STORE_GLOBAL 3 ; @Shape
  0005  L5  PUSH_GLOBAL 3 ; @Shape
  0006  L5  PUSH_GLOBAL 0 ; @AnyRef
  0007  L5  PUSH_GLOBAL 1 ; @Any
  0008  L6  MAKE_FN 1 ; -> block 1
  0009  L5  MAKE_FN 2 ; -> block 2
  0010  L5  MAKE_CLASS 4 ; class Rect @Rect parents=3 members=[area,<init>]
  0011  L5  STORE_GLOBAL 5 ; @Rect
  0012  L8  PUSH_GLOBAL 5 ; @Rect
  0013  L8  PUSH_GLOBAL 3 ; @Shape
  0014  L8  PUSH_GLOBAL 0 ; @AnyRef
  0015  L8  PUSH_GLOBAL 1 ; @Any
  0016  L9  MAKE_FN 3 ; -> block 3
  0017  L8  MAKE_FN 4 ; -> block 4
  0018  L8  MAKE_CLASS 6 ; class Square @Square parents=4 members=[describe,<init>]
  0019  L8  STORE_GLOBAL 7 ; @Square
  0020  L11  PUSH_GLOBAL 0 ; @AnyRef
  0021  L11  PUSH_GLOBAL 1 ; @Any
  0022  L11  MAKE_FN 5 ; -> block 5
  0023  L11  MAKE_CLASS 8 ; class Unit1 @Unit1.type parents=2 members=[<init>]
  0024  L11  STORE_GLOBAL 9 ; @Unit1.type
  0025  L11  MAKE_FN 6 ; -> block 6
  0026  L11  MAKE_LAZY 0
  0027  L11  STORE_GLOBAL 10 ; Unit1
  0028  L14  PUSH_GLOBAL 11 ; println
  0029  L14  PUSH_GLOBAL 7 ; @Square
  0030  L14  PUSH_CONST 12 ; 2
  0031  L14  NEW 13 ; <init>/1
  0032  L14  SEND 14 ; describe/0
  0033  L14  CALL 1
  0034  L14  POP 0
  0035  L15  PUSH_GLOBAL 11 ; println
  0036  L15  PUSH_GLOBAL 10 ; Unit1
  0037  L15  FORCE 0
  0038  L15  SEND 15 ; one/0
  0039  L15  CALL 1
  0040  L15  POP 0
  0041  L1  PUSH_UNIT 0
  0042  L1  RETURN 0

  function describe arity=1 method locals=0 stack=2 captures=0
    0000  L3  PUSH_CONST 0 ; "shape with area "
    0001  L3  PUSH_LOCAL 0
    0002  L3  SEND 1 ; area/0
    0003  L3  CONCAT 2
    0004  L3  RETURN 0

  function area arity=1 method locals=0 stack=2 captures=0
    0000  L6  PUSH_LOCAL 0
    0001  L6  SEND 0 ; w/0
    0002  L6  PUSH_LOCAL 0
    0003  L6  SEND 1 ; h/0
    0004  L6  MUL 0
    0005  L6  SEND_APPLY 2 ; toDouble/0
    0006  L6  RETURN 0

  function Rect.<init> arity=3 method locals=0 stack=2 captures=0
    0000  L5  PUSH_LOCAL 1
    0001  L5  SEND 0 ; toDouble/0
    0002  L5  STORE_LOCAL 1
    0003  L5  PUSH_LOCAL 2
    0004  L5  SEND 0 ; toDouble/0
    0005  L5  STORE_LOCAL 2
    0006  L5  PUSH_LOCAL 1
    0007  L5  PUSH_LOCAL 2
    0008  L5  STORE_FIELDS_IF_NEW 1 ; [w,h]
    0009  L5  PUSH_LOCAL 0
    0010  L5  RETURN 0

  function describe arity=1 method locals=0 stack=2 captures=0
    0000  L9  PUSH_CONST 0 ; "square, "
    0001  L9  PUSH_LOCAL 0
    0002  L9  SEND_SUPER 1 ; super.describe/0 in @Square
    0003  L9  ADD 0
    0004  L9  RETURN 0

  function Square.<init> arity=2 method locals=0 stack=4 captures=0
    0000  L8  PUSH_LOCAL 1
    0001  L8  SEND 0 ; toDouble/0
    0002  L8  STORE_LOCAL 1
    0003  L8  PUSH_LOCAL 1
    0004  L8  STORE_FIELD_IF_NEW 1 ; Square::side
    0005  L8  PUSH_GLOBAL 2 ; @Rect
    0006  L8  PUSH_LOCAL 0
    0007  L8  PUSH_LOCAL 1
    0008  L8  PUSH_LOCAL 1
    0009  L8  INVOKE_INIT 3 ; <init>/2
    0010  L8  STORE_LOCAL 0
    0011  L8  PUSH_LOCAL 0
    0012  L8  RETURN 0

  function Unit1.<init> arity=1 method locals=0 stack=1 captures=0
    0000  L12  PUSH_CONST 0 ; 1
    0001  L12  STORE_FIELD 1 ; one
    0002  L11  PUSH_LOCAL 0
    0003  L11  RETURN 0

  function <object Unit1> arity=0 locals=0 stack=1 captures=0
    0000  L11  PUSH_GLOBAL 0 ; @Unit1.type
    0001  L11  NEW 1 ; <init>/0
    0002  L11  RETURN 0
```

- Each template is built in dependency order: `Shape` (parents `@AnyRef`,
  `@Any`), `Rect` (parents `@Shape`, `@AnyRef`, `@Any`), `Square` (`@Rect`
  first). The pushed parents are the linearization.
- `def area: Double = w * h`: the declared `Double` widens the result
  (`SEND_APPLY toDouble/0`), and `Rect.<init>` widens its `Double` parameters
  on entry (`SEND toDouble/0`, `STORE_LOCAL`), because protoScala has no
  static types (D4) and `Int` arguments would otherwise stay integers.
- `Square.<init>`: its parameter `side` is stored under the private key
  `Square::side` (`STORE_FIELD_IF_NEW`, D5); then `INVOKE_INIT <init>/2` runs
  `Rect`'s constructor on `this` (slot 0) with `side, side` and stores the
  result back into slot 0 (0005-0010).
- `super.describe` is `SEND_SUPER` with the site `super.describe/0 in
  @Square`: the search starts after `Square` in the receiver's
  linearization. `"square, " + ...` is an `ADD` whose slow path
  concatenates.
- `object Unit1`: its class `@Unit1.type`, its holder `Unit1`; the read
  `Unit1.one` is `PUSH_GLOBAL Unit1`, `FORCE`, `SEND one/0`.

### Example 19: spreading a list into arguments

<!-- vm-example: 19-spread -->
```scala
def total(xs: Int*): Int = xs.sum

case class Bag(items: Int*)

val nums = List(1, 2, 3)
println(total(nums*))
println(Bag(nums*).items)
```

```text
function <top> arity=0 locals=0 stack=5 captures=0
  0000  L3  PUSH_GLOBAL 0 ; @Product
  0001  L3  PUSH_GLOBAL 1 ; @AnyRef
  0002  L3  PUSH_GLOBAL 2 ; @Any
  0003  L3  MAKE_FN 0 ; -> block 0
  0004  L3  MAKE_CLASS 3 ; class Bag @Bag parents=3 members=[<init>] fields=[items]
  0005  L3  STORE_GLOBAL 4 ; @Bag
  0006  L3  PUSH_GLOBAL 1 ; @AnyRef
  0007  L3  PUSH_GLOBAL 2 ; @Any
  0008  L3  MAKE_FN 1 ; -> block 1
  0009  L3  MAKE_FN 2 ; -> block 2
  0010  L3  MAKE_FN 3 ; -> block 3
  0011  L3  MAKE_CLASS 5 ; class Bag @Bag.type parents=2 members=[apply,unapply,<init>]
  0012  L3  STORE_GLOBAL 6 ; @Bag.type
  0013  L3  MAKE_FN 4 ; -> block 4
  0014  L3  MAKE_LAZY 0
  0015  L3  STORE_GLOBAL 7 ; Bag
  0016  L1  MAKE_FN 5 ; -> block 5
  0017  L1  STORE_GLOBAL 8 ; total
  0018  L5  PUSH_GLOBAL 9 ; List
  0019  L5  PUSH_CONST 10 ; 1
  0020  L5  PUSH_CONST 11 ; 2
  0021  L5  PUSH_CONST 12 ; 3
  0022  L5  CALL 3
  0023  L5  STORE_GLOBAL 13 ; nums
  0024  L6  PUSH_GLOBAL 14 ; println
  0025  L6  PUSH_GLOBAL 8 ; total
  0026  L6  PUSH_GLOBAL 13 ; nums
  0027  L6  CALL_SPREAD 0
  0028  L6  CALL 1
  0029  L6  POP 0
  0030  L7  PUSH_GLOBAL 14 ; println
  0031  L7  PUSH_GLOBAL 4 ; @Bag
  0032  L7  PUSH_GLOBAL 13 ; nums
  0033  L7  NEW_SPREAD 15 ; <init>/0
  0034  L7  SEND 16 ; items/0
  0035  L7  CALL 1
  0036  L7  POP 0
  0037  L1  PUSH_UNIT 0
  0038  L1  RETURN 0

  function Bag.<init> arity=2 variadic method locals=0 stack=1 captures=0
    0000  L3  PUSH_LOCAL 1
    0001  L3  STORE_FIELD_IF_NEW 0 ; items
    0002  L3  PUSH_LOCAL 0
    0003  L3  RETURN 0

  function apply arity=2 variadic method locals=0 stack=2 captures=0
    0000  L3  PUSH_GLOBAL 0 ; @Bag
    0001  L3  PUSH_LOCAL 1
    0002  L3  NEW_SPREAD 1 ; <init>/0
    0003  L3  RETURN 0

  function unapply arity=2 method locals=0 stack=1 captures=0
    0000  L3  PUSH_LOCAL 1
    0001  L3  RETURN 0

  function Bag.<init> arity=1 method locals=0 stack=1 captures=0
    0000  L3  PUSH_LOCAL 0
    0001  L3  RETURN 0

  function <object Bag> arity=0 locals=0 stack=1 captures=0
    0000  L3  PUSH_GLOBAL 0 ; @Bag.type
    0001  L3  NEW 1 ; <init>/0
    0002  L3  RETURN 0

  function total arity=1 variadic locals=0 stack=1 captures=0
    0000  L1  PUSH_LOCAL 0
    0001  L1  SEND 0 ; sum/0
    0002  L1  RETURN 0
```

- `total(nums*)` is `CALL_SPREAD 0`: no fixed arguments, then the list. The
  callee is `variadic`: its slot 0 receives the arguments as a `List`.
- `Bag(nums*)` compiles to `NEW_SPREAD <init>/0` on `@Bag`; the synthesised
  `apply` of the companion does the same with its own repeated parameter.

### Example 20: `null`, `&&` / `||`, named arguments to a method, `catch` on `Exception`

<!-- vm-example: 20-operators-and-named-sends -->
```scala
class Greeter:
  def greet(name: String, punct: String = "!"): String = "Hi " + name + punct

def check(x: Int, s: String): Boolean = x >= 0 && s != null || x == -1

val g = new Greeter()
println(g.greet("Bo", punct = "?"))
println(check(1, null))
try throw new RuntimeException("boom")
catch case e: Exception => println(e.getMessage)
```

```text
function <top> arity=0 locals=2 stack=4 captures=0
  0000  L1  PUSH_GLOBAL 0 ; @AnyRef
  0001  L1  PUSH_GLOBAL 1 ; @Any
  0002  L2  MAKE_FN 0 ; -> block 0
  0003  L1  MAKE_FN 1 ; -> block 1
  0004  L1  MAKE_CLASS 2 ; class Greeter @Greeter parents=2 members=[greet,<init>]
  0005  L1  STORE_GLOBAL 3 ; @Greeter
  0006  L4  MAKE_FN 2 ; -> block 2
  0007  L4  STORE_GLOBAL 4 ; check
  0008  L6  PUSH_GLOBAL 3 ; @Greeter
  0009  L6  NEW 5 ; <init>/0
  0010  L6  STORE_GLOBAL 6 ; g
  0011  L7  PUSH_GLOBAL 7 ; println
  0012  L7  PUSH_GLOBAL 6 ; g
  0013  L7  PUSH_CONST 8 ; "Bo"
  0014  L7  PUSH_CONST 9 ; "?"
  0015  L7  SEND_KW 10 ; greet/1(punct=)
  0016  L7  CALL 1
  0017  L7  POP 0
  0018  L8  PUSH_GLOBAL 7 ; println
  0019  L8  PUSH_GLOBAL 4 ; check
  0020  L8  PUSH_CONST 11 ; 1
  0021  L8  PUSH_NULL 0
  0022  L8  CALL 2
  0023  L8  CALL 1
  0024  L8  POP 0
  0025  L9  PUSH_GLOBAL 12 ; @RuntimeException
  0026  L9  PUSH_CONST 13 ; "boom"
  0027  L9  NEW 14 ; <init>/1
  0028  L9  THROW 0
  0029  L9  PUSH_UNIT 0
  0030  L9  JUMP 17 ; -> 48
  0031  L10  PUSH_LOCAL 0
  0032  L10  TEST_PROTO 15 ; @Exception
  0033  L10  DUP 0
  0034  L10  JUMP_IF_TRUE 4 ; -> 39
  0035  L10  POP 0
  0036  L10  PUSH_LOCAL 0
  0037  L10  TEST_PROTO 16 ; @Throwable
  0038  L10  NOT 0
  0039  L10  JUMP_IF_FALSE 7 ; -> 47
  0040  L10  PUSH_LOCAL 0
  0041  L10  STORE_LOCAL 1
  0042  L10  PUSH_GLOBAL 7 ; println
  0043  L10  PUSH_LOCAL 1
  0044  L10  SEND 17 ; getMessage/0
  0045  L10  CALL 1
  0046  L10  JUMP 1 ; -> 48
  0047  L9  RETHROW 0
  0048  L9  POP 0
  0049  L1  PUSH_UNIT 0
  0050  L1  RETURN 0
  handler [25, 30) -> 31  depth=0 slot=0 catch

  function greet arity=3 method locals=0 stack=2 captures=0
    0000  L2  PUSH_CONST 0 ; "Hi "
    0001  L2  PUSH_LOCAL 1
    0002  L2  ADD 0
    0003  L2  PUSH_LOCAL 2
    0004  L2  ADD 0
    0005  L2  RETURN 0

    function greet$default$1 arity=3 method locals=0 stack=1 captures=0
      0000  L2  PUSH_CONST 0 ; "!"
      0001  L2  RETURN 0

  function Greeter.<init> arity=1 method locals=0 stack=1 captures=0
    0000  L1  PUSH_LOCAL 0
    0001  L1  RETURN 0

  function check arity=2 locals=0 stack=2 captures=0
    0000  L4  PUSH_LOCAL 0
    0001  L4  PUSH_CONST 0 ; 0
    0002  L4  GE 0
    0003  L4  JUMP_IF_FALSE 4 ; -> 8
    0004  L4  PUSH_LOCAL 1
    0005  L4  PUSH_NULL 0
    0006  L4  NE 0
    0007  L4  JUMP 1 ; -> 9
    0008  L4  PUSH_FALSE 0
    0009  L4  JUMP_IF_TRUE 4 ; -> 14
    0010  L4  PUSH_LOCAL 0
    0011  L4  PUSH_CONST 1 ; -1
    0012  L4  EQ 0
    0013  L4  JUMP 1 ; -> 15
    0014  L4  PUSH_TRUE 0
    0015  L4  RETURN 0
```

- `g.greet("Bo", punct = "?")` is `SEND_KW` (0015) with the site
  `greet/1(punct=)`; the method's prologue binds `punct` by name. Its default
  block is `greet$default$1`, `arity=3` because a method's slot 0 is `this`.
- `check`: `&&` is `JUMP_IF_FALSE` to a `PUSH_FALSE` (0003, 0008) and `||` is
  `JUMP_IF_TRUE` to a `PUSH_TRUE` (0009, 0014); `s != null` is `PUSH_NULL` and
  `NE`; `x >= 0` is `GE`.
- A top-level `try` works: `<top>` has two local slots (the caught value and
  `e`) and a handler table of its own, `[25, 30) -> 31`.
- `case e: Exception` (0031-0039): `TEST_PROTO @Exception`, and when that is
  false, `DUP`/`JUMP_IF_TRUE`/`POP` fall into a second test, "not a Scala
  `Throwable`", so a foreign runtime's exception is also selected (D134).

## 4. How to add an opcode

The model is `CALL_KW` (80), the most recent call opcode, and the binary
fast paths. Every place to touch, in order:

1. **The enum,** `src/compiler/Opcodes.h`: take the next free value in the
   range of its phase (40 for a general opcode, 83 for the object model, 98
   for exceptions) and document its operand and stack effect in the comment.
   Never renumber: the value is the encoding, and the precompiled prelude
   image (`src/support/PreludeImage.h`) stores raw code words.
   ```cpp
   CALL_KW        = 80,  // [f a1..an v1..vm] -> [r]     operand: KwSendSite constant
   ```
2. **The name,** `opName` in `src/compiler/BytecodeModule.cpp`. The switch has
   no `default`, so GCC and Clang warn about a missing case.
3. **The disassembler,** `commentFor` in the same file: put the opcode in the
   group that decodes its operand (a constant, a symbol, a jump target).
4. **The compiler:** emit it with `emit(Op::X, operand, pos, stackEffect)` in
   `Compiler.cpp`, `CompilePatterns.cpp` or `CompileTemplates.cpp`. The
   **stack effect is mandatory and must be exact**: it is the only source of
   `maxStack` (1.2). A forward jump is `emitJump` + `patchJumpTo`; a backward
   one `emitJumpBack`. `CALL_KW`'s site, in `compileApply`:
   ```cpp
   emit(Op::CALL_KW,
        fn_->mod->addKwSendSite("apply", static_cast<std::uint32_t>(positional), keywords),
        a.pos, -static_cast<int>(a.args.size()));
   ```
5. **The opcode body,** `src/runtime/OpcodeOps.h`: anything beyond a slot
   move goes here, once, so the interpreter and the transpiler share it. A
   body that needs a private member of `ExecutionEngine` reaches it through
   the `ops::Engine` forwards. Add the opcode to the class list (V, P or B)
   in the header comment.
6. **The VM,** the `switch` in `ExecutionEngine::runLoop`: decode the
   operand, call the body, adjust `sp`. Rooting: operands stay in their stack
   slots until the result replaces them (`base[0] = r; sp = base + 1;`);
   never hold a `ProtoObject*` only in a C++ local across an allocation or a
   safepoint (protoCore EMBEDDER-CONFORMANCE rule 3). A call opcode sets
   `pendingBase` before the call and resets it after, or a `FutureYield`
   through it cannot be resumed (1.6). An opcode the switch does not handle
   reaches `std::logic_error("unknown opcode ...")`.
   ```cpp
   case Op::CALL_KW: {
       const auto& site = mod.constAt(operand);
       const proto::ProtoObject** base =
           sp - site.argc - static_cast<unsigned>(site.nameSymbols.size()) - 1;
       pendingBase = static_cast<unsigned>(base - slots);
       base[0] = ops::Engine::callKeywords(*this, &frame, base, site);
       pendingBase = kNoPendingCall;
       sp = base + 1;
       continue;
   }
   ```
7. **The transpiler,** `src/compiler/CppEmitter.cpp`: its stack-effect table,
   its `check()` (an opcode it cannot translate must be refused there, not
   mis-translated) and its emission switch; a `gen::` forward in
   `src/runtime/GeneratedSupport.cpp` and `include/protoScala/GeneratedModule.h`
   when generated code needs a new entry point. `docs/PROTOSCALAC_SPECIFICATION.md`
   §8 lists the opcode coverage.
8. **The prelude image:** `protoscala-precompile` serialises modules word by
   word (`src/tools/PrecompileMain.cpp`); a new **constant kind** or module
   field must be added to the image format and to `src/runtime/PreludeImage.cpp`.
   `tests/unit/test_prelude_image.cpp` compares the image with a fresh
   compilation.
9. **Tests:** a unit test in `tests/unit/test_bytecode.cpp` or
   `test_compiler.cpp` for the encoding, a conformance fixture under
   `tests/conformance/` for the behaviour, and a
   `docs/vm-examples/NN-name.scala` whose listing shows the opcode, with its
   marker in this document. Run `disasm_examples.py ... --update`, then
   `ctest -R docs/vm-examples`.
10. **This document and STATUS.md:** the opcode's row in section 2 (and the
    count), the opcode table of `docs/STATUS.md`, and the notes of any example
    whose listing changed.

## 5. How to add a primitive

A primitive is a native method: a C++ function with protoCore's
`ProtoMethod` signature, installed either on the prototype of a built-in
type (`"abc".length`, `xs.head`) or on the globals object (`println`).

**The context rule (maintainer's rule).** A `ProtoContext` is created only
when a method, that is interpreted code with its own locals, is activated; a
primitive runs in its caller's context. **`main` does not follow this rule
today.** Every native call made by the VM goes through
`ExecutionEngine::callNative`, which creates a scope context per call
(ExecutionEngine.cpp:165) and copies the arguments into its slots. The other
places that create a context for something that is not a method activation:

| Where | What |
|---|---|
| ExecutionEngine.cpp:165 | `callNative`: every native method call (`CALL` of a native, `SEND`/`SEND_APPLY` of a native member, a native constructor) |
| ExecutionEngine.cpp:126 | `sendSmalltalk`: a protoST message; `callNative` then adds a second context |
| ExecutionEngine.cpp:215 | `invoke` of a bound method: a scope for `[self, args]`, then the frame |
| ExecutionEngine.cpp:238 | `send`: every C++-initiated send, including `slowBinary` (an operator on a user class) and `NEG`/`NOT` on a non-primitive |
| ExecutionEngine.cpp:367 | `callWithReceiver` with named arguments to a native method |
| ExecutionEngine.cpp:677 | `sendKeywords` (`SEND_KW`), for native and Scala callees alike |
| ExecutionEngine.cpp:722, 734 | `callKeywords` (`CALL_KW`); a second one for a bound method |
| ExecutionEngine.cpp:1210, 1349 | `CALL_SPREAD`, `NEW_SPREAD` argument scopes |
| ExecutionEngine.cpp:1326 | `NEW` with named arguments: a scope that is created and never used (section 6) |
| ExecutionEngine.cpp:443, 460, 502 | `construct`, `makeClass` (`MAKE_CLASS`), `makeTuple` (`MAKE_TUPLE`) |
| ExecutionEngine.cpp:408 | `forceMember` (the first read of a `lazy val` member), then the thunk's frame |
| PrimitiveSupport.h:98, 126, 162 | `ListBuilder` (one per collection result), `callOne` (one per callback invocation, besides the callee's frame), `addFlat`'s probe |

`Primitives.cpp` and `CollectionPrimitives.cpp` open further scopes inside
individual primitives (`grep -n "ProtoContext [a-z]*(" src/runtime/*.cpp`).
Write new primitives so that they are correct either way: allocate only in
the `ctx` you are given or in a scope you open, and root what you hold
(step 4).

1. **Write the function** with the `PRIM` macro of
   `src/runtime/PrimitiveSupport.h`, which expands to the `ProtoMethod`
   signature: `ctx`, `self` (the receiver; `nullptr` for a global function),
   `args` (positional arguments), and an unnamed `ProtoKeywordArguments*`.
   Read arguments with `argCount`, `expectArgs`, `arg`, `intArg`,
   `stringArg`; raise errors with `throw ScalaError("ClassName", "message")`,
   which a handler receives as an instance of the prelude class of that name
   (`materialiseError`). Model, `string_length` in `src/runtime/Primitives.cpp`:
   ```cpp
   PRIM(string_length) { expectArgs(ctx, args, "length", 0); return ctx->fromInteger(stringLength(ctx, self)); }
   ```
   A native that uses named arguments declares the raw signature instead,
   because `PRIM` discards the fifth parameter (`prim_kwprobe_call`).
2. **Register it.** Add `{"name", &prim}` to the right table of
   `installPrimitives` (`ints`, `doubles`, `strings`, `globals`, ...;
   collections in `installCollectionPrimitives`, products in
   `ProductPrimitives.cpp`). `installAll` wraps each entry with
   `ctx->fromMethod(nullptr, fn)` and sets it on the prototype. A **global**
   function must also be declared to the compiler: add its name to
   `builtinGlobalNames()` in `src/support/BuiltinNames.cpp`, or the compiler
   reports `Not found`. Prefer a method on a prototype, or Scala code in
   `lib/prelude.scala`, when C++ speed is not needed.
3. **Calling convention.** The receiver is `self`; the arguments are in
   `args` (the slots of the scope context `callNative` filled). Return a
   value, never `nullptr` (the VM turns `nullptr` into `null`). A primitive
   that calls back into Scala uses `activeCallContext()->engine->invoke(ctx,
   f, argv, n)` (or `callOne` for one argument), with `argv` in traced slots;
   it may throw `FutureYield` through you, so do not catch `...`.
4. **Rooting** (protoCore EMBEDDER-CONFORMANCE rule 3): no `ProtoObject*` may
   be held only in a C++ local across an allocation or a `safepoint()`. The
   arguments are rooted by the caller for the whole call. A value you create
   and then keep while you allocate again must sit in a traced slot: open a
   scope (`proto::ProtoContext scope(ctx->space, ctx);
   scope.resizeAutomaticLocals(n); scope.setAutomaticLocal(i, v)`) or use
   `ListBuilder` for a list; set `scope.returnValue` to the result before the
   scope ends, so it survives the scope's destructor.
5. **Test fixture.** Add `tests/conformance/<area>/<name>.scala`; the glob in
   `tests/CMakeLists.txt` makes it one CTest case. The first line is the
   directive: `// EXPECT: <text>` (exit 0, last non-empty line of standard
   output equals the text), `// EXPECT-ERROR[: <text>]`, or `// XFAIL: ...`
   for a known deviation (`tests/conformance/run.sh`). Cover the error cases
   too.
   ```scala
   // EXPECT: 3
   @main def run(): Unit = println("abc".length)
   ```
6. **Docs:** `docs/LANGUAGE.md` lists the library surface; a departure from
   Scala is a `D<n>` entry in `docs/STATUS.md`.

## 6. Known oddities

Found while writing this document, verified against `main` at `3ac106e8`.
Two found then are fixed since: `@tailrec` was ignored, and top-level
destructuring stored its temporary in a hidden global `<t0>`.

- **`NOP` (0) is never emitted** by the compiler, the transpiler or the
  prelude image builder. The VM and the emitter accept it. It is dead.
- **`EXTEND` (1) is reachable only for operands above 16,777,215**, which
  needs more than 2^24 constants, blocks, slots or arguments in one function;
  it is covered by `tests/unit/test_bytecode.cpp`, not by any program.
- **The STATUS.md opcode table omits `STORE_FIELD_IF_NEW` (81)** and still
  marks 81..95 as reserved, although 81 and 82 are in use.
- **Stale comments.** `Opcodes.h` and STATUS.md describe `CALL_KW`'s keyword
  arguments as a `ProtoSparseList` keyed by symbol addresses, and so does the
  comment above `prim_kwprobe_call`; the VM passes a `ProtoKeywordArguments`
  of (name, value) pairs (`SiteKeywords` in `ExecutionEngine.cpp`). The class
  list at the top of `OpcodeOps.h` does not mention `STORE_FIELDS_IF_NEW`
  (82), and `CppEmitter.h` says "the 60 opcodes" where there are 61. The
  comment at the top of `tests/unit/test_prelude_image.cpp` says
  `disassemble()` prints the constant pool; it prints each instruction's
  constant as a comment, not the pool.
- **An unused context in `NEW` with named arguments.** The `NEW` case of
  `runLoop` (ExecutionEngine.cpp:1326) creates a `ProtoContext scope` and
  then calls `instantiate(&frame, ...)`; the scope is never used.
- **A pattern definition rebuilds the tuple.** `val (a, b) = (1, 2)`
  matches the tuple, builds a second tuple of the bound values in a temporary
  (a slot: a local of the block, or of the top-level frame at the top level)
  and reads `a` and `b` back with `_1` / `_2` sends (example 09). The result
  is correct; binding the variables straight from the pattern would save the
  second tuple.
- **No poll at frame entry.** Unlike protoClojure, the VM polls
  `safepoint()` only at `JUMP_BACK` (1.6).
- **The disassembler does not list the constant pool, slot names or default
  blocks as such.** Constants appear as comments on the instructions that use
  them; a default block is recognisable only by its `$default$` name.
