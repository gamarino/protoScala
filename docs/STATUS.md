# protoScala Status

> Living tracker of the gap between [LANGUAGE.md](LANGUAGE.md) and the
> implementation. Update it with every change.
>
> **Current state (2026-09-22):** Phase 0 complete — skeleton only. The binary
> answers `--version` / `--help`; it does not run Scala code yet.
> **Tests:** 2 unit, 3 CLI, 1 conformance (XFAIL). Last verified 2026-09-22.

## Implemented

- [x] CMake build against protoCore (`PROTO_CORE_PREFIX` or sibling tree)
- [x] `protoscala --version`, `--help`; unknown flags exit 2
- [x] GoogleTest unit harness, conformance runner (`// EXPECT:` directives), CLI tests

## Not yet implemented

Everything in LANGUAGE.md. By phase: see [ROADMAP.md](ROADMAP.md).

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

## Open bugs

None.

## Known issues / platform dependencies

See DESIGN §11 (R1–R8). `Map`/`Set` depend on Phase P1.

## History

See [CHANGELOG.md](../CHANGELOG.md).
