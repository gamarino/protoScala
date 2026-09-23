# Installing protoScala

protoScala is a dynamic Scala 3 dialect on the protoCore runtime. It is a
consumer of protoCore, never a bundler of it: `bin/protoscala` links
`libprotoCore.so.2`, and every package protoScala produces declares a runtime
dependency on protoCore's own package instead of shipping a copy.

---

## Prerequisites

- A **C++20** compiler (GCC or Clang).
- **CMake** 3.20 or newer.
- **libreadline** (`libreadline-dev` on Debian/Ubuntu, `readline-devel` on
  Fedora/RHEL, `brew install readline` on macOS). It is a hard requirement of
  the interactive REPL: configuration fails with a `FATAL_ERROR` without it.
- **protoCore 2.1.0 or newer**, installed, with its CMake package
  configuration. See protoCore's `docs/INSTALLATION.md`.

**Why 2.1.0 and not 2.0.0**, which the other four runtimes accept: protoScala's
actor mailbox is built on protoCore's `ProtoMPSCQueue`, and protoCore gained it
in 2.1.0. Building against 2.0.x would compile the CAS'd-`ProtoList` mailbox
seam instead, silently changing the concurrency implementation.

---

## Building against an installed protoCore

```bash
# protoCore installed in a default prefix: nothing to pass.
cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release

# protoCore installed elsewhere.
cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release -DPROTO_CORE_PREFIX=$HOME/.local
# equivalently
cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$HOME/.local

cmake --build build_release -j4
ctest --test-dir build_release --output-on-failure
```

The discovery is `find_package(protoCore 2.1 CONFIG)`, so the prefix must hold
`lib/cmake/protoCore/protoCoreConfig.cmake`. **A prefix holding only
`libprotoCore` and `protoCore.h` is no longer accepted**: without the package
configuration there is no way to tell protoCore 1.x from 2.x, nor 2.0 from 2.1.

protoCore's version compatibility is `SameMajorVersion`, and the requested minor
version is a floor, so `2.1` accepts any `2.x` from `2.1.0` up and refuses
`2.0.x`, `1.x` and `3.x`. protoScala additionally asserts that the package's
`SOVERSION` is `2`.

### Selecting the actor mailbox backend

`PROTOSCALA_HAS_PMQ` is set from the protoCore **version**, not from a substring
search of `protoCore.h`. The configure output names the version it decided on:

```
-- protoScala: installed protoCore 2.1.0 (SOVERSION 2) from .../lib/cmake/protoCore
-- protoScala: actor mailboxes on protoCore ProtoMPSCQueue (protoCore 2.1.0)
```

The earlier probe grepped whichever `protoCore.h` the discovery found first for
`newMPSCQueue`, so which mailbox implementation was compiled in depended on the
discovery mode — an installed 2.1 package and a sibling source tree could
disagree without saying so.

## Building against a sibling developer tree

When no installed package is found *and* no prefix was named, protoScala falls
back to the sibling source tree `../protoCore`, searching `build_release`, then
`build`, then `build_check`. The fallback prints a `WARNING` and must not be used
to produce a distributable package. It still checks the ABI: it requires
`libprotoCore.so.2` beside the library it found, and it reads the version out of
`../protoCore/CMakeLists.txt`'s `project()` call and refuses anything older than
2.1.0.

Pass `-DPROTOCORE_REQUIRE_PACKAGE=ON` to turn the fallback into a hard error.
**Every packaging build sets it.** Switching a build directory between the two
modes leaves a stale `PROTOCORE_LIBRARY` cache entry; delete the build directory
rather than reconfiguring in place.

---

## Installing

```bash
cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=$HOME/.local -DCMAKE_PREFIX_PATH=$HOME/.local
cmake --build build_release -j4
cmake --install build_release --component protoScala
```

Installed layout, relative to the prefix:

| Content | Location |
|---------|----------|
| `protoscala` | `bin/` |
| `LICENSE`, `README.md` | `share/doc/protoScala/` |

**`protoscala` needs no external data file at run time.** `lib/prelude.scala` is
read at configure time and compiled into the binary as a generated
`PreludeSource.cpp`, so there is no build-tree / install-tree path that can
differ and nothing to go missing from a package.

protoScala installs **no** copy of protoCore. `bin/protoscala` carries the
install RPATH `$ORIGIN/../<libdir>` (`@executable_path/../<libdir>` on macOS), so
a protoCore installed into the same prefix is found with no `LD_LIBRARY_PATH`.

---

## Packages (CPack)

```bash
cmake -S . -B build_pkg -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=<protocore-prefix> -DPROTOCORE_REQUIRE_PACKAGE=ON
cmake --build build_pkg -j4
cd build_pkg && cpack -G DEB
```

Generators are chosen at configure time; the DEB and RPM generators are enabled
only when `dpkg` and `rpmbuild` are found, because `cpack` aborts the whole run
when a generator's tool is missing and would take the TGZ down with it. Each
configure prints whether a generator was enabled or disabled, and why.

Package names are pinned rather than left to each generator's default casing:
`protoscala` for DEB, `protoScala` for RPM. Both declare a bounded dependency on
protoCore's own package, with the 2.1.0 floor the mailbox requires:

| Format | Relation |
|--------|----------|
| DEB | `Depends: protocore (>= 2.1.0), protocore (<< 3.0.0)` |
| RPM | `Requires: protoCore >= 2.1.0, protoCore < 3.0.0` |

`CPACK_DEBIAN_PACKAGE_SHLIBDEPS` is deliberately **not** enabled. It would name
libreadline's and libstdc++'s packages automatically, but `dpkg-shlibdeps`
resolves every `NEEDED` entry to the distribution package that owns it, and no
distribution owns `libprotoCore.so.2`; it fails with "cannot find library" and
takes the whole `.deb` down with it. `libreadline` is therefore not declared,
which matches protoST and protoClojure.

### Platform verification status

| Platform | Packaging | Status |
|----------|-----------|--------|
| Linux | TGZ, DEB (needs `dpkg`), RPM (needs `rpmbuild`) | Built, installed to a scratch prefix and smoke-tested, including the extracted `.deb` payload |
| macOS | DragNDrop | Configured and reviewed, **never built** — no macOS host |
| Windows | NSIS, ZIP | Configured and reviewed, **never built** — no Windows host |

RPM packaging is configured and reviewed but **never executed**: `rpmbuild` is
not installed on the host this was verified on.
