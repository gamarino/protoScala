# CLAUDE.md — protoScala

Guidance for Claude Code (and any agent) working in this repository.

## What this is

A dynamic Scala 3 dialect on protoCore, and a platform-validation project for
protoCore. Read `docs/DESIGN.md` before any change; `docs/STATUS.md` is the
source of truth for what is implemented.

## Build and test

```bash
cmake -B build_release -S .          # finds ../protoCore/build_release
cmake --build build_release
ctest --test-dir build_release --output-on-failure
```

- `build_release/` is the canonical build directory.
- After any protoCore change, rebuild protoScala **from clean**
  (`rm -rf build_release` first, path checked): stale binaries against a new
  protoCore ABI crash.
- Conformance fixtures: `tests/conformance/NN-topic/*.scala`, first line
  `// EXPECT: ...`, `// EXPECT-ERROR[: ...]`, `// XFAIL: ...`. One CTest case
  per file. Unit tests: GoogleTest in `tests/unit/`.

## Rules

- **Design decisions go to the maintainer.** Language semantics not fixed by
  the Scala 3 reference, data representation, GC or threading protocol,
  runtime limits: write the options down and ask; never decide silently.
- **Principles P1–P6** (DESIGN §1.1) are binding: values live in protoCore
  structures, one `ProtoContext` per invocation, missing capability extends
  protoCore (as a new type, not by changing an existing model), document
  every deviation (`D<n>` in STATUS.md), purity over performance, no
  mutable-heap GC premises.
- **Never map transient data to `ProtoTuple`** — every tuple node is interned
  and perennial.
- **Never write outside `/home/gamarino/Documentos/proyectos`** — no `/tmp`,
  no `$HOME` edits, no `cmake --install` outside the workspace. Scratch files
  go in `../.agent_scratch/<task>/`.
- Benchmarks self-report the work they did and the runner verifies it.
- Commit with the repository's configured git identity; do not override it.
- Docs, comments and messages in professional English.

## Platform work

Phases P1, C and S modify protoCore, protoClojure and protoST. Follow each
repository's own conventions and CLAUDE.md, and rebuild every embedder from
clean after a protoCore ABI change (protoJS: no parallel `-j` builds, run
test262 sequentially).
