# Installing protoScala

protoScala is a dynamic Scala 3 dialect on the protoCore runtime. It is a
consumer of protoCore, never a bundler of it: `bin/protoscala` links
`libprotoScala.so.1`, which links **`libprotoCore.so.3`**, and every package
protoScala produces declares a runtime dependency on protoCore's own package
instead of shipping a copy. (`objdump -p` on either file shows the chain; it
matters for packaging, because a dependency scanner that looks only at the
executable sees no protoCore at all.)

---

## Prerequisites

- A **C++20** compiler (GCC or Clang; MSVC from Visual Studio 2022 on
  Windows, see [Windows (MSVC)](#windows-msvc)).
- **CMake** 3.20 or newer.
- **libreadline** (`libreadline-dev` on Debian/Ubuntu, `readline-devel` on
  Fedora/RHEL, `brew install readline` on macOS). It is a hard requirement of
  the interactive REPL: configuration fails with a `FATAL_ERROR` without it
  (except on Windows, where the REPL uses the console's own line editing).
- **protoCore 2.7.0 or newer** (2.7.0 is where `proto::proto_long` first exists; the "Why 2.6.1" floors below still hold underneath it), installed, with its CMake package configuration.
  **On Windows the floor is 2.9.0**: it is the release that honours
  `ProtoSpace::setThreadStackBytes` there, which gives the actor workers the
  evaluator's 32 MiB stack (§Windows). CI builds and tests against **2.10.2 on
  Linux, macOS and Windows**, and against **2.7.0**, the declared minimum, in
  one Linux job (the floor job; the Windows minimum, 2.9.0, has no job of its
  own); the packaging sections below were
  verified against 2.5.0. See protoCore's `docs/INSTALLATION.md`.
- **protoIO 0.2.2 or newer within 0.2** (the I/O layer the protoCore runtimes
  share; 0.2.2 brings `process::run` options, crash statuses as 128 + signal on
  Windows too, the Windows certificate store for TLS and a dual-stack listener), either
  installed (`protoio-dev`, or any prefix holding `lib/cmake/protoIO/`, named
  with `-DCMAKE_PREFIX_PATH`), a build tree named with `-DprotoIO_DIR=<protoIO>/
  build_release`, or the source tree checked out beside this one as
  `../protoIO`, which the build then compiles as part of protoScala's (CMake
  **3.21** is needed for that). It is linked **statically** into
  `libprotoScala.so`, so an installed protoScala does not depend on protoIO.
- **OpenSSL 3** development files (`libssl-dev` on Debian/Ubuntu,
  `openssl-devel` on Fedora/RHEL), for protoIO's TLS and https. At run time
  the package depends on `libssl3`, which `dpkg-shlibdeps` derives from the
  library (§Packages).
- **Running the test suite on macOS** needs GNU `timeout`, which macOS does
  not ship: `brew install coreutils` and put `$(brew --prefix coreutils)/libexec/gnubin`
  on `PATH` (the cross-platform CI job does exactly that).

**Why 2.6.1.** Three floors apply and the highest one wins:

- protoScala's actor mailbox is built on protoCore's `ProtoMPSCQueue`, which
  protoCore gained in **2.1.0**. Building against 2.0.x would compile the
  CAS'd-`ProtoList` mailbox seam instead, silently changing the concurrency
  implementation. That is the floor `CMakeLists.txt` names
  (`PROTOCORE_MIN_VERSION_FULL "2.1.0"`), and it is the version its diagnostics
  and the DEB's `Depends` still quote.
- protoScala is built against **`PROTOCORE_ABI_SOVERSION 3`**, and
  `CMakeLists.txt` asserts it (`FATAL_ERROR` on mismatch). SOVERSION went
  `2` → `3` in protoCore **2.2.0**, so **2.1.0 cannot build this tree**:
  configuration stops naming both numbers.
- Since the I/O track the actor pool **creates threads from worker threads**
  while workers block in I/O, and before protoCore **2.6.1**
  `ProtoSpace::newThread` did that by replacing the space's main context with a
  temporary one, so the main program's roots stopped being scanned. 2.6.1 is
  API- and ABI-compatible (SOVERSION 3), so only the version check can tell.

Until 2026-09-27 this page said "protoCore 2.1.0 or newer" throughout and admitted
the SOVERSION change only in a Known-defect section 200 lines further down, so a
reader who followed the prerequisite hit that `FATAL_ERROR`. The 2.1.0 floor in
`CMakeLists.txt` is deliberately left as it is for now: the same variable is the
DEB's dependency floor, and raising that is the packaging change recorded under
"Known defect: the DEB dependency floor does not encode the ABI" as the
maintainer's to take.

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

The discovery is `find_package(protoCore 2.6.1 CONFIG)`, so the prefix must hold
`lib/cmake/protoCore/protoCoreConfig.cmake`. **A prefix holding only
`libprotoCore` and `protoCore.h` is no longer accepted**: without the package
configuration there is no way to tell protoCore 1.x from 2.x, nor 2.0 from 2.1.

protoCore's version compatibility is `SameMajorVersion`, and the requested minor
version is a floor, so `2.6.1` accepts any `2.x` from `2.6.1` up and refuses
older `2.x`, `1.x` and `3.x`. protoScala additionally asserts that the package's
`SOVERSION` is **`3`**.

### Selecting the actor mailbox backend

`PROTOSCALA_HAS_PMQ` is set from the protoCore **version**, not from a substring
search of `protoCore.h`. The configure output names the version it decided on:

```
-- protoScala: installed protoCore 2.5.0 (SOVERSION 3) from .../lib/cmake/protoCore
-- protoScala: actor mailboxes on protoCore ProtoMPSCQueue (protoCore 2.5.0)
```

`protoscala --version` reports the same decision at run time:
`protoScala 0.6.0 (actor mailboxes: ProtoMPSCQueue)`.

The earlier probe grepped whichever `protoCore.h` the discovery found first for
`newMPSCQueue`, so which mailbox implementation was compiled in depended on the
discovery mode — an installed 2.1 package and a sibling source tree could
disagree without saying so.

## Building against a sibling developer tree

When no installed package is found *and* no prefix was named, protoScala falls
back to the sibling source tree `../protoCore`, searching `build_release`, then
`build`, then `build_check`. The fallback prints a `WARNING` and must not be used
to produce a distributable package. It still checks the ABI: it requires
`libprotoCore.so.3` beside the library it found — the file named by
`PROTOCORE_ABI_SOVERSION`, so this check is what rejects a SOVERSION-2 sibling
tree — and it reads the version out of `../protoCore/CMakeLists.txt`'s `project()`
call and refuses anything older than 2.1.0.

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
On Windows it runs `nmake`, `cl` and `link` from Visual Studio (2022 verified, or its
Build Tools with the C++ workload): from a Developer Command Prompt directly, and
otherwise through the environment `vswhere` finds (§Windows).

**Three environment overrides**, each replacing the corresponding baked-in value:

| Variable | Replaces |
|---|---|
| `PROTOSCALAC_CXX` | the C++ compiler the generated `Makefile` invokes |
| `PROTOSCALAC_INCLUDE_DIRS` | the `-I` directories (`:`-separated; `;` on Windows) |
| `PROTOSCALAC_LIBRARY_DIRS` | the `-L` / `-rpath` directories (`:`-separated; `;` on Windows) |

Without them, `protoscalac` decides between the build tree's directories and the
installation's by comparing its own location with the build tree's, so a relocated prefix
keeps working.

**The generated target is always `module.so`** (`module.dll` on Windows). Rename it to
`<module>.so` before putting it on a module search path: `CompiledModuleProvider` resolves
the logical path `a.b.C` to `<base>/a/b/C.so`, so a file still called `module.so` is only
reachable through `protoscala --run-module module.so` (a relative path is taken against
the working directory).

**Where a compiled module is looked for**, in order:

1. each `:`-separated (`;` on Windows) entry of `PROTOSCALA_MODULE_PATH`, a relative
   one against the working directory;
2. `<prefix>/<libdir>/protoscala/modules`.

`protoscala --version` prints that list. Compiled modules are searched **after** source
modules, so a `.scala` beside a `.so` still wins and installing a compiled module cannot
change the meaning of an `import` that already resolved.

---

## Windows (MSVC)

protoScala builds and runs natively on Windows with Visual Studio 2022 (MSVC
19.44 verified, Windows 11 and the `windows-2022` CI runner), using the CMake
and Ninja that ship with it. Build protoCore **2.9.0 or later** first (its
`docs/INSTALLATION.md`, "Windows (MSVC)") and install it into a prefix; protoIO
is compiled from the sibling `../protoIO` as on Linux. From an "x64 Native Tools
Command Prompt":

```bat
set PREFIX=%LOCALAPPDATA%\Programs\proto
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PREFIX_PATH=%PREFIX% -DCMAKE_INSTALL_PREFIX=%PREFIX% ^
      "-DOPENSSL_ROOT_DIR=C:/Program Files/OpenSSL-Win64"
cmake --build build
ctest --test-dir build -j4
cmake --install build
%PREFIX%\bin\protoscala --version
```

**Debug and Release do not mix.** A Debug build uses the debug C++ runtime
(`/MDd`) and a Release build the release one (`/MD`); `protoScala.dll` and
protoCore's DLL exchange `std::string` and C++ exceptions, so both must be built
with the same one. Configuration stops with a message naming both when the
protoCore package holds no build of the kind requested (CMake would otherwise
silently pick the other one).

**OpenSSL.** Any OpenSSL 3 for Windows with headers, import libraries and its
`bin\*.dll` works as `OPENSSL_ROOT_DIR` (the Shining Light build the CI runner
has, or the one PostgreSQL ships). The two DLLs are looked up by the names the
version found gives them (`libcrypto-3-x64.dll`, `libssl-3-x64.dll`), and
configuration **fails** when they are not there, rather than shipping whatever
a wildcard matched; the version is printed and goes into the name of the
licence file the package carries. Its licence file is found in the OpenSSL
directory, or named with `-DPROTOSCALA_OPENSSL_LICENSE_FILE=<file>`.

**What an installation holds.** The build copies the DLLs protoScala needs into
`build/bin/`, so `protoscala.exe` and the tests run in place. `cmake --install`
(and `cpack -G ZIP`, which packs the same files) is **self-contained**, because
Windows has no package manager to bring a dependency in:

| Where | What |
|---|---|
| `bin/` | `protoscala.exe`, `protoscalac.exe`, `protoScala.dll`; protoCore's DLL (`protoCore-3.dll` since protoCore 2.9.0 -- whatever file the imported target names); `libcrypto-3-x64.dll`, `libssl-3-x64.dll`; the Visual C++ runtime DLLs (`msvcp140.dll`, `vcruntime140.dll`, ..., through CMake's `InstallRequiredSystemLibraries`, so a machine without the Visual C++ Redistributable runs it too) |
| `lib/`, `include/` | `protoScala.lib`, `protoCore.lib`, `protoScala/GeneratedModule.h`, `protoCore.h`: what `protoscalac --build-so` compiles and links a module against |
| `share/doc/protoScala/` | `LICENSE`, `README.md`, `OpenSSL-<version>-LICENSE.txt` |

With that `bin` on `PATH`, `protoscala` runs scripts and the REPL from
`cmd.exe` or PowerShell. `cpack -G ZIP` produces `protoscala-<version>-win64.zip`;
CI unpacks it into an empty directory and runs `protoscala.exe` there with only
Windows' own directories on `PATH`, then builds a module with the unpacked
`protoscalac.exe` and runs it. An NSIS installer is added to the generators only
when `makensis` is found.

How Windows differs, by design:

- **Same output bytes everywhere.** The standard streams are binary, so
  `println` writes `\n` as on Linux, and the console is switched to UTF-8.
  `sys.props` reports `os.name` `Windows`, `os.arch` `amd64`, the real
  `os.version`, `user.home` / `user.name` from `USERPROFILE` / `USERNAME`,
  `line.separator` `\n`, `file.separator` `/` (Windows accepts it in every
  path) and `path.separator` `;`.
- **UTF-8 throughout.** `protoscala.exe` carries a manifest that makes UTF-8
  the process code page (Windows 10 1903 or later), so arguments, environment
  variables and file names with non-ASCII characters work as on Linux.
- **Path lists use `;`** (`PROTOSCALA_MODULE_PATH`, `PROTOSCALA_PROVIDERS`,
  `PROTOSCALAC_INCLUDE_DIRS`, `PROTOSCALAC_LIBRARY_DIRS`), as `PATH` does,
  since drive letters contain `:`. Provider plug-ins and compiled modules are
  `.dll` files (the suffix is matched without regard to case). A plug-in exports
  its two ABI functions with `PROTOSCALA_PROVIDER_EXPORT`
  (`src/umd/ProviderPlugins.h`); a module's entry points carry
  `PROTOSCALA_MODULE_EXPORT` (`<protoScala/GeneratedModule.h>`).
- **No readline.** The console edits the line and keeps a history itself, so
  the REPL reads plain lines and keeps no history file.
- **Deep recursion reaches the same depth everywhere.** The evaluator thread
  reserves 32 MiB, and so do the actor workers that run every actor and
  `Future` body: protoScala sets protoCore's `ProtoSpace::setThreadStackBytes`,
  which protoCore 2.9.0 honours on Windows (whose default is 1 MiB) as on Linux
  and macOS. `tests/conformance/14-futures/deep-recursion-in-an-actor-and-a-future.scala`
  checks that a worker recurses as deep as the main program. The stack limit
  comes from `GetCurrentThreadStackLimits`, and the overflow raises
  `StackOverflowError`.
- **Other programs.** `Process(...)` and `"cmd".!` keep Scala's semantics: the
  command is split on spaces and run directly, **not** through a shell, so a
  `cmd.exe` built-in such as `dir` or `echo` is not a program there (as on the
  JVM). protoIO refuses to run a `.bat` or `.cmd` file directly, because
  `cmd.exe` would re-parse its arguments; run `cmd /c <file>` explicitly when
  that is what is meant. A child ended by an unhandled exception or by
  `destroy()` reports 128 + the corresponding signal number, as on POSIX.
- **`protoscalac` builds modules with the Microsoft toolset**: an NMake
  `Makefile`, `nmake`, `cl` and `link`, producing `module.dll`
  (docs/PROTOSCALAC_SPECIFICATION.md §1). It needs the Developer environment,
  or finds it with `vswhere`.

**Test harness.** The script tests run through Git for Windows' `bash`, and
every test registered on Linux is registered on Windows too, the transpiled
twins (`transpiled/*`, on when `nmake` is found at configure time) and the
transpiler CLI checks included. The scripts avoid what that `bash` cannot
give a native program: it rewrites a POSIX path placed in the environment
(`/bin/sh` becomes `C:/Program Files/Git/usr/bin/sh`), and a named FIFO it
creates is not readable by a native program, so `cli/io-stdin` streams
through a coprocess's pipes. `tests/transpile-exclude-windows.txt` lists the
fixtures whose transpiled twin cannot pass on Windows (none).
On the `windows-2022` runner (CI run 37109119193, 2026-10-03, protoCore 2.10.2) **2428 tests
are registered and the 2425 that CI runs pass** (0 failed, 7 skipped: the
embedder-conformance rules that need process isolation; the three
clock-dependent isolate cases run in their own informational job on Linux).
That includes the 977 `transpiled/*` and 12 `benchmarks-transpiled/*` cases,
each building a `module.dll` with `cl`. macOS registers the same 2428; Linux
registers 2429, the one more being `umd/protost-interop`, which needs protoST
beside the tree.

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
protoCore's own package. The floor is **2.6.1** (§Prerequisites: `ProtoMPSCQueue`
needs 2.1.0, `PROTOCORE_ABI_SOVERSION 3` needs 2.2.0, and creating threads from
workers needs 2.6.1, so the highest binds). It read 2.1.0 until 2026-09-27, which
named a protoCore that cannot build this tree, and 2.2.0 until the I/O track:

| Format | Relation |
|--------|----------|
| DEB | `Depends: protocore (>= 2.6.1), protocore (<< 3.0.0)` |
| RPM | `Requires: protoCore >= 2.6.1, protoCore < 3.0.0` |

protoIO is a static library, so it is **not** a package dependency; what it
brings at run time is OpenSSL (`libssl3`), which `dpkg-shlibdeps` adds from
`libprotoScala.so`'s `NEEDED` entries.

`CPACK_DEBIAN_PACKAGE_SHLIBDEPS` is **`ON`** (`CMakeLists.txt`), so
`dpkg-shlibdeps` runs over the package's ELF files and adds the dependencies it
derives to the hand-written range above. This page said "deliberately **not**
enabled" here and "now `ON`" eighty lines below until 2026-09-27; `ON` is the
truth, and the consequence the old sentence drew — "`libreadline` is therefore not
declared" — went with it.

What it produces now, and how to check it without installing anything:

```bash
# from a directory holding a minimal debian/control (Source:/Package:/Architecture:)
dpkg-shlibdeps -O --ignore-missing-info \
    <staged>/usr/bin/protoscala <staged>/usr/lib/*/libprotoScala.so.1
```

gives
`libc6 (>= 2.38), libgcc-s1 (>= 3.0), libreadline8t64 (>= 6.0), libstdc++6 (>= 13), protocore (>= 2.5.0)`
on a host where protoCore 2.5.0 is installed from its own DEB. Two things about
that line matter:

- **`libreadline` is declared** after all, and so is a real SONAME-derived
  `protocore` dependency.
- It appears **only when the shared library is scanned too**. `bin/protoscala`
  `NEEDED`s just `libprotoScala.so.1`; the protoCore SONAME is one level down. The
  same run over the executable alone emits no `protocore` entry at all.

The `protocore (>= 2.5.0)` half is new and comes from the **producer**: protoCore
now ships a `DEBIAN/shlibs` (`libprotoCore 3 protocore (>= 2.5.0)`, from
`CPACK_DEBIAN_PACKAGE_GENERATE_SHLIBS` with a `>=` policy). Before that,
`SHLIBDEPS` alone produced nothing for protoCore, silently — see the Known defect
below. The `.deb` artefacts built in this tree on 2026-09-27 predate protoCore's
`shlibs` and carry only the hand-written range, so a package rebuilt today is not
the package sitting in `build_pkg*/`.

### Platform verification status

Last verified 2026-09-27 against protoScala 0.6.0 and protoCore 2.5.0
(`PROTOCORE_ABI_SOVERSION 3`), built with `-DPROTOCORE_REQUIRE_PACKAGE=ON` so the
sibling developer fallback was a hard error.

| Platform | Packaging | Status |
|----------|-----------|--------|
| Linux / Debian-Ubuntu | TGZ, DEB | **VERIFIED.** Installed with `dpkg -i` as root in a throwaway `ubuntu:24.04` container; `protoscala` ran a script and `protoscalac --build-so` compiled, linked and produced a loadable `module.so` there, outside any repository, with no `LD_LIBRARY_PATH` and no `PROTOSCALAC_INCLUDE_DIRS` set. |
| Linux / Fedora-RHEL | TGZ, RPM | **VERIFIED.** `cpack -G RPM` executed in a throwaway `fedora:41` container (glibc 2.40, `rpm` 4.20.1); the RPM installed with `rpm -i` and `protoscala` ran correctly there. This closes the gap left by decision D-I2. |
| macOS | DragNDrop | **UNVERIFIED.** Configured and reviewed only; there is no macOS host here. Review is not verification. |
| Windows | ZIP, NSIS | **ZIP VERIFIED IN CI** (2026-10-03, run 37109119193, `windows-2022`, MSVC, protoCore 2.10.2, protoIO 0.2.2, OpenSSL 3.6.4). Built and tested (2425/2425 run, see [Windows (MSVC)](#windows-msvc)); `cpack -G ZIP` builds the ZIP, CI checks it holds protoCore's DLL, OpenSSL's DLLs and licence and the C++ runtime, unpacks it into an empty directory, runs `protoscala.exe` there with only Windows' own directories on `PATH`, and builds and runs a module with the unpacked `protoscalac.exe`. Earlier, by hand (2026-10-01, Windows 11, MSVC 19.44): `cmake --install`, then `protoscala --version`, a script and the REPL from `cmd.exe`. The NSIS installer is generated when `makensis` is found but has not been installed and run. |

### Packaging and installation defects: the whole list, in one place

Five distinct issues came out of the 2026-09-27 end-to-end installer work, in four
different states, and they were described across two documents with no
cross-reference — "two packaging defects" in
[ROADMAP.md](ROADMAP.md) §Phase 7 means **P1 and P2 only**.

| # | Defect | State |
|---|---|---|
| P1 | `libprotoScala.so*` was installed in CMake's `Unspecified` component, so `cmake --install --component protoScala` — the command this page gives — installed `protoscalac` but not the library it links | **Fixed.** `COMPONENT protoScala` is now repeated on every artifact clause ([STATUS.md](STATUS.md) §"Phase 7 — packaging", item 1) |
| P2 | `protoscalac` compiled a generated module against a stale `/usr/local/include/protoCore.h` when the prefix held no header, producing a module that linked, loaded and segfaulted with no diagnostic | **Fixed.** `protoscalac` refuses when it cannot find `protoCore.h` or `protoScala/GeneratedModule.h` on its own include path, and names the directories it searched (T0-21) |
| P3 | T0-21 does not fire when protoCore is installed under `/usr` and a stale header remains in `/usr/local/include`: GCC searches `/usr/local/include` first and ignores a `-I` naming a standard system directory | **Not fixed, and not fixable with a compiler flag.** Reproduced; remedies are to remove the stale header or to install protoCore under a non-system prefix (next section) |
| P4 | The DEB's `Depends` was a version range and not an ABI check, so it admitted a protoCore whose SONAME this package was not linked against | **Fixed 2026-09-27**, in two independent ways. The floor rose to `2.2.0`, the release that carries `SOVERSION 3`, so the range no longer admits a `SOVERSION 2` protoCore. And since protoCore began shipping a `shlibs` file, `dpkg-shlibdeps` derives `protocore (>= 2.5.0)` from the SONAME itself (§Packages), which cannot drift from the binary the way a hand-written range did |
| P5 | The DEB does not refresh the shared-library cache | **protoCore's: fixed** — its package now generates a `postinst` that runs `ldconfig`. **protoScala's: not fixed** — it ships no maintainer script |

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
- `CPACK_DEBIAN_PACKAGE_SHLIBDEPS` is `ON`, and **as of protoCore `ccce990f` it
  now emits a real dependency**: `protocore (>= 2.5.0)`, derived from the SONAME.
  The history is worth keeping, because it says where the missing piece was. With
  the flag on and nothing else, `dpkg-shlibdeps` resolved `libprotoCore.so.3` to
  the `protocore` package (`dpkg -S` agrees), found that the package shipped no
  `shlibs`/`symbols` control file, and — because CPack passes
  `--ignore-missing-info` — dropped the entry **silently** instead of failing the
  build. The fix was in the producer: protoCore now ships a `DEBIAN/shlibs`
  reading `libprotoCore 3 protocore (>= 2.5.0)`, and a re-run of
  `dpkg-shlibdeps` over a staged protoScala tree emits that dependency (see
  §Packages above for the exact command and output).
  **This narrows the defect rather than closing it**: the emitted floor is the
  protoCore version present on the *build* host, not a statement about the ABI, and
  it still sits beside a hand-written range whose floor is `2.1.0`. A protoScala
  DEB built against protoCore 2.2.0 would emit `protocore (>= 2.2.0)`, which is
  correct for SOVERSION 3 by coincidence of when the SONAME changed.

### Known defect: protoScala's DEB does not refresh the shared-library cache

**protoCore's does, since `ccce990f`.** `CPACK_DEBIAN_PACKAGE_GENERATE_SHLIBS`
makes CPack generate maintainer scripts as well as the `shlibs` file, so the
installed `protocore` package carries a `postinst` that runs `ldconfig` on
`configure`, and `ldconfig -p` does list `libprotoCore.so.3` after `dpkg -i`.
Verified on this host from dpkg's own database
(`/var/lib/dpkg/info/protocore.postinst` and `ldconfig -p | grep protoCore`).
Until 2026-09-27 this section said neither package carried one.

**protoScala's package still carries no maintainer script.** It does not set
`CPACK_DEBIAN_PACKAGE_GENERATE_SHLIBS`, and the control archive of the `.deb`
built here on 2026-09-27 holds only `control` and `md5sums`
(`dpkg-deb --ctrl-tarfile … | tar t`). So `libprotoScala.so.1` is not registered in
the cache by installing protoScala. Programs still start, because each binary
carries `RUNPATH $ORIGIN/../${CMAKE_INSTALL_LIBDIR}` and because the library lands
in a directory the dynamic loader searches by default, but the cache is misleading.
Run `ldconfig` after installing, or set that CPack option — which would also give
protoScala's own library a `shlibs` file, for whatever links against it next. The
RPM has no such defect.
