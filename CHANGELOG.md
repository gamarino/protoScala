# Changelog

All notable changes to protoScala are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow the
phase scheme in [docs/ROADMAP.md](docs/ROADMAP.md).

## [Unreleased]

### Added
- Design specification (`docs/DESIGN.md`), language reference
  (`docs/LANGUAGE.md`), roadmap, status tracker and interop design.
- Platform specifications for protoCore: `ProtoSparseListObject`
  (`docs/platform/PSLO-SPEC.md`) and `ProtoMPSCQueue`
  (`docs/platform/PMQ-SPEC.md`).
- Actor model on protoClojure's design with the `actor-bench.sh` suite
  (DESIGN §8).
- Implementation plans for Phase P1 and Phase 1 (`docs/plans/`).
- Phase 0 skeleton: CMake build against protoCore, `protoscala --version` /
  `--help`, GoogleTest unit harness, conformance runner with `// EXPECT:`
  directives, CLI tests.
