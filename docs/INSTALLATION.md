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
| `protoscalac` (the transpiler) | `bin/` |
| `libprotoScala.so.1` | `<libdir>/` |
| `protoScala/GeneratedModule.h` (the whole published C++ surface) | `include/` |
| `find_package(protoScala CONFIG)` files | `<libdir>/cmake/protoScala/` |
| provider plug-ins (protoScala ships none) | `<libdir>/protoscala/providers/` |
| compiled modules | `<libdir>/protoscala/modules/` |
| `LICENSE`, `README.md` | `share/doc/protoScala/` |

The last two directories are installed **empty**, on purpose: `protoscala --version`
prints both paths, and a printed path that does not exist leaves a user unable to tell
"nothing is installed here" from "the runtime is looking somewhere else".

**`protoscala` needs no external data file at run time.** `lib/prelude.scala` is
read at configure time and compiled into the binary as a generated
`PreludeSource.cpp`, so there is no build-tree / install-tree path that can
differ and nothing to go missing from a package.

protoScala installs **no** copy of protoCore. `bin/protoscala` and `bin/protoscalac`
carry the install RPATH `$ORIGIN/../<libdir>` (`@executable_path/../<libdir>` on macOS),
so a protoCore installed into the same prefix is found with no `LD_LIBRARY_PATH`.

### `protoscalac`, the transpiler

**What it needs, and when.** `--emit-cpp` (the default) needs nothing beyond the
installed files above. `--build-so` runs `make` and a C++ compiler, so those two are a
package **recommendation** rather than a dependency: most users of the interpreter never
reach that mode, and a hard dependency would pull a toolchain onto every installation.

**Three environment overrides**, each replacing the corresponding baked-in value:

| Variable | Replaces |
|---|---|
| `PROTOSCALAC_CXX` | the C++ compiler the generated `Makefile` invokes |
| `PROTOSCALAC_INCLUDE_DIRS` | the `-I` directories (`:`-separated) |
| `PROTOSCALAC_LIBRARY_DIRS` | the `-L` / `-rpath` directories (`:`-separated) |

Without them, `protoscalac` decides between the build tree's directories and the
installation's by comparing its own location with the build tree's, so a relocated prefix
keeps working.

**The generated target is always `module.so`.** Rename it to `<module>.so` before putting
it on a module search path: `CompiledModuleProvider` resolves the logical path `a.b.C` to
`<base>/a/b/C.so`, so a file still called `module.so` is only reachable through
`protoscala --run-module ./module.so`.

**Where a compiled module is looked for**, in order:

1. each `:`-separated entry of `PROTOSCALA_MODULE_PATH`;
2. `<prefix>/<libdir>/protoscala/modules`.

`protoscala --version` prints that list. Compiled modules are searched **after** source
modules, so a `.scala` beside a `.so` still wins and installing a compiled module cannot
change the meaning of an `import` that already resolved.

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

Last verified 2026-09-27 against protoScala 0.6.0 and protoCore 2.5.0
(`PROTOCORE_ABI_SOVERSION 3`), built with `-DPROTOCORE_REQUIRE_PACKAGE=ON` so the
sibling developer fallback was a hard error.

| Platform | Packaging | Status |
|----------|-----------|--------|
| Linux / Debian-Ubuntu | TGZ, DEB | **VERIFIED.** Installed with `dpkg -i` as root in a throwaway `ubuntu:24.04` container; `protoscala` ran a script and `protoscalac --build-so` compiled, linked and produced a loadable `module.so` there, outside any repository, with no `LD_LIBRARY_PATH` and no `PROTOSCALAC_INCLUDE_DIRS` set. |
| Linux / Fedora-RHEL | TGZ, RPM | **VERIFIED.** `cpack -G RPM` executed in a throwaway `fedora:41` container (glibc 2.40, `rpm` 4.20.1); the RPM installed with `rpm -i` and `protoscala` ran correctly there. This closes the gap left by decision D-I2. |
| macOS | DragNDrop | **UNVERIFIED.** Configured and reviewed only; there is no macOS host here. Review is not verification. |
| Windows | NSIS, ZIP | **UNVERIFIED.** Configured and reviewed only; there is no Windows host here. |

### T0-21 has a blind spot when protoCore is installed under `/usr`

`protoscalac` refuses to emit a build when it cannot find `protoCore.h` on its
own include list (T0-21), because compiling a generated module against a
different `protoCore.h` produces a module that links and then crashes. That
refusal fires, and was confirmed to fire by removing the header from the prefix.

It does **not** protect the case where protoCore is installed under `/usr` and a
stale `protoCore.h` remains in `/usr/local/include`:

- GCC's default `<...>` search order puts `/usr/local/include` **before**
  `/usr/include`, and GCC **ignores** a `-I` that names a standard system
  directory. So the `-I/usr/include` protoscalac emits for a `/usr` install
  cannot out-rank `/usr/local/include`.
- T0-21 sees that `/usr/include/protoCore.h` exists, so nothing looks wrong and
  the refusal never fires.

Reproduced with the real February 2026 `/usr/local/include/protoCore.h` in place
and protoCore 2.5.0 installed under `/usr`: `protoscalac` exited 0, the generated
module compiled against `/usr/local/include/protoCore.h`, linked against the
correct `libprotoCore.so.3`, and `dlopen` of the result succeeded. Right library,
wrong header, clean build, no diagnostic — which is exactly the configuration
T0-21 was written to prevent. (The crash itself was not reproduced: a generated
module refuses to be entered outside a protoScala runtime, so the precondition
was demonstrated and the crash was not.)

There is no compiler flag that fixes this, because `-isystem /usr/include`
breaks the standard C++ header search. The reliable remedies are to remove the
stale `/usr/local/include/protoCore.h`, or to install protoCore under a prefix
that is not a default system directory so that `-I` is honoured.

### Known defect: the DEB dependency floor does not encode the ABI

The `Depends` field is a *version range*, and on its own that range is not an ABI
check. `PROTOCORE_ABI_SOVERSION` went from `2` to `3` in protoCore **2.2.0**, so
protoCore 2.0.0 and 2.1.0 carry `libprotoCore.so.2` while 2.2.0 and later carry
`libprotoCore.so.3`. A floor of ``2.1.0`` therefore admits a protoCore whose
SONAME this package was not linked against.

This was demonstrated, not argued. A decoy `protocore` 2.1.0 package providing
only `libprotoCore.so.2` was installed in a container; `dpkg -i` then accepted
this package, and the installed binary failed to start with
`libprotoCore.so.3: cannot open shared object file`. The install succeeded and
the program did not run.

Two things limit the damage, and one closes it:

- At **build** time the failure is loud, not silent. `find_package(protoCore …)`
  alone does accept a SOVERSION-2 protoCore, but `CMakeLists.txt` follows it with
  an explicit `protoCore_SOVERSION` assertion against `PROTOCORE_ABI_SOVERSION`,
  which stops configuration with a `FATAL_ERROR` naming both numbers. Verified by
  configuring against a complete forged 2.1.0 / SOVERSION 2 prefix.
- The **RPM** does not have this hole. `rpm` generates
  `Requires: libprotoCore.so.3()(64bit)` automatically from the linked binary, and
  that requirement is on the SONAME rather than the version. Verified: the decoy
  protoCore 2.1.0 does not satisfy it and `rpm -i` refuses.
- Raising the DEB floor to `2.2.0`, the first protoCore that shipped SOVERSION 3,
  would make the DEB range agree with the ABI. That is a packaging change for the
  maintainer to take, and it is not made here.

### Known defect: the DEB does not refresh the shared-library cache

Neither this package nor protoCore's carries a `postinst` or an `ldconfig`
trigger, so `ldconfig -p` does not list `libprotoCore.so.3` after `dpkg -i`.
Programs still start, because each binary carries
`RUNPATH $ORIGIN/../${CMAKE_INSTALL_LIBDIR}` and because the library lands in a
directory the dynamic loader searches by default, but the cache is misleading.
Run `ldconfig` after installing. The RPM has no such defect.
