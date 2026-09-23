# Phase I — Native Installers for protoCore and the Five Runtimes

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce native installers for Linux, macOS and Windows covering protoCore and all five runtimes built on it (protoPython, protoJS, protoST, protoClojure, protoScala), on the maintainer's premise that **the target machine already has protoCore installed**. No runtime bundles protoCore; each one declares a *versioned, ABI-checked* runtime dependency on protoCore's package. The Linux packages are built, installed into a scratch prefix inside the workspace and smoke-tested there; the macOS and Windows packaging is configured, reviewed and explicitly marked unverified, because no such host exists here.

**Architecture:** One producer and six consumers.

- **protoCore becomes a real CMake package.** It already writes `install(TARGETS protoCore EXPORT protoCoreTargets ...)` (`protoCore/CMakeLists.txt:209-216`) but never emits the matching `install(EXPORT ...)`, so `protoCoreConfig.cmake` does not exist and `/usr/local/lib/cmake` is empty. Task 1 adds `install(EXPORT)`, `configure_package_config_file`, `write_basic_package_version_file` with `SameMajorVersion` compatibility, an explicit ABI assertion keyed on `SOVERSION`, and a pkg-config `protoCore.pc`.
- **Every runtime prefers that package.** Each one calls `find_package(protoCore 2.0 QUIET CONFIG)` first and links the imported target `protoCore::protoCore`. The existing sibling-directory discovery (`../protoCore/build_release`, `../protoCore/build`, `../protoCore/build_check`) stays, demoted to an explicit *developer* fallback that runs only when no installed package was found and no prefix was named, warns loudly, and can be forbidden with `-DPROTOCORE_REQUIRE_PACKAGE=ON`.
- **Packaging stays where each repo already put it.** protoPython, protoST, protoClojure and protoScala package with CPack; protoJS keeps its hand-built `packaging/` pipeline (DEB by `dpkg-deb`, RPM spec, macOS preinstall, WiX MSI) and only has its version floors corrected. Nothing is migrated between the two worlds.
- **Two portability gaps are closed as part of the deliverable**, because a package that installs a broken program is not a deliverable: protoST's stdlib self-location (Linux-only today) gains the macOS and Windows branches protoPython already has, and protoCore's NSIS installer starts recording its version and ABI in the registry, which is the only thing protoJS's WiX condition can actually test on Windows.

**Tech Stack:** CMake ≥ 3.16 (protoCore, protoJS) / ≥ 3.20 (protoPython, protoST, protoClojure) / ≥ 3.20 (protoScala; its file says 3.20), `CMakePackageConfigHelpers`, `GNUInstallDirs`, CPack (TGZ, DEB, RPM, DragNDrop, NSIS, ZIP), `dpkg-deb`, pkg-config, WiX v3, C++20.

**Spec:** this document, plus the packaging survey of 2026-09-23 (the read-only scratch file `packaging-survey.md`, whose findings are reproduced inline here with the line numbers re-verified against the real files while this plan was written).

---

## Global Constraints

- **Workspace safety.** Create, modify or delete nothing outside `/home/gamarino/Documentos/proyectos`. **Never touch `/usr/local`** — a root-owned protoCore `1.0.0` lives there and is outside the workspace. Never run `sudo`. Never run `cmake --install` without an explicit `--prefix` or `DESTDIR` pointing inside the workspace. Scratch output goes to `/home/gamarino/Documentos/proyectos/.agent_scratch/phase-i-installers/`.
- **No system installs.** Every install, package extraction and smoke test in this plan targets `$S/stage` or `$S/debroot` under that scratch directory. `dpkg -i`, `rpm -i`, `apt`, `brew` and `ldconfig` are out of scope.
- **protoJS build parallelism: no `-j` at all**, and test262 runs strictly sequentially (`TEST262_CONCURRENCY=1`, `ctest -j1`). Parallel protoJS builds and parallel test262 sweeps have hung this machine. **Ask the user before any full test262 sweep.** Every other repo builds with `-j4` maximum.
- **`rpmbuild` is not installed on this machine** (`which rpmbuild` returns nothing; `dpkg` and `dpkg-deb` are present at `/usr/bin`). RPM generation is therefore configured and reviewed but not executed here. This is the concrete failure D-I2 fixes: three repos enable the RPM generator unconditionally and `cpack` aborts on this host.
- **Git.** Each repository commits with **its own configured identity** — they differ: protoST is `Gustavo Marino <gamarino@gmail.com>`, the other five are `Gustavo Marino <gamarino@numaes.com>`. Never override `user.name` or `user.email`. Stage explicitly by path (`git add <file> <file>`), never `git add -A` or `git add .`. **Nothing is pushed by the implementer.**
- **A concurrent agent is committing to protoScala `main`.** Before editing `protoScala/CMakeLists.txt`, re-read it and re-check the line numbers cited in Task 5. If the file has changed under the plan, stop and re-plan Task 5 rather than force the diff.
- **protoCore's version is not bumped in this phase.** Another plan (`2026-09-23-phase-p2-protompscqueue.md`) bumps protoCore `2.0.0 → 2.1.0`. This phase adds only additive packaging machinery, so it writes an `## [Unreleased]` CHANGELOG entry and leaves `project(protoCore VERSION 2.0.0 ...)` (`protoCore/CMakeLists.txt:8`) alone. A packaging change that collides with that bump is a merge conflict for no benefit.
- **Branches.** One branch per repository, all named `feature/phase-i-installers`. Six branches, six independent commits, none pushed.
- All documentation, code comments and commit messages in professional English.
- End every commit message with:

  ```
  Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
  ```

---

## Task 0: Decisions

Taken by the maintainer's delegate on 2026-09-23 and **approved by the maintainer on 2026-09-23**. Each one is argued below the table.

| ID | Decision | Status |
|---|---|---|
| **D-I1** | protoCore emits a real CMake package config (`install(EXPORT)` + `configure_package_config_file` + `write_basic_package_version_file` with `SameMajorVersion` + an explicit `SOVERSION` assertion) and a pkg-config `.pc`. Every runtime prefers `find_package(protoCore 2.0 REQUIRED CONFIG)`; sibling-directory discovery is kept as an explicit developer fallback used only when no installed package is found. | [agent, approved by the maintainer on 2026-09-23] |
| **D-I2** | The unconditional `DEB;RPM` CPack generators in protoPython, protoST and protoClojure (and protoJS, see below) are guarded on `find_program(dpkg)` / `find_program(rpmbuild)`, following protoCore's own pattern at `protoCore/CMakeLists.txt:229-247`. | [agent, approved by the maintainer on 2026-09-23] |
| **D-I3** | protoScala gains CPack **now**, not in a later phase as `protoScala/CMakeLists.txt:177` says, because the maintainer's scope is all five runtimes. | [agent, approved by the maintainer on 2026-09-23] |
| **D-I4** | protoJS keeps its standalone `packaging/` pipeline; it is not migrated to CPack. Its hardcoded protoCore `>= 1.0.0` checks are raised to `>= 2.0.0` with a `< 3.0.0` ceiling and a `SOVERSION 2` file check. | [agent, approved by the maintainer on 2026-09-23] |
| **D-I5** | protoST's stdlib self-location gains the macOS (`_NSGetExecutablePath`) and Windows (`GetModuleFileNameA`) branches, ported from protoPython's `getExecutablePath()`. | [agent, approved by the maintainer on 2026-09-23] |
| **D-I6** | Linux packages are built, installed into a scratch prefix inside the workspace and smoke-tested there. macOS and Windows packaging is configured and reviewed but marked **UNVERIFIED** in the plan and in each repo's installation documentation. | [agent, approved by the maintainer on 2026-09-23] |
| **D-I7** | The root-owned protoCore `1.0.0` at `/usr/local/lib` is left untouched. D-I1's version check is what protects a build from picking it up, and Task 8 Step 8 proves it by configuring a runtime against `/usr/local` alone and showing a clear version error instead of a silent link. | [agent, approved by the maintainer on 2026-09-23] |

### D-I1 — protoCore emits a CMake package config. **This is a prerequisite, not scope creep.**

The maintainer's premise for this whole phase is "the target machine already has protoCore installed". Today that sentence cannot be checked by anything in the tree. `install(TARGETS protoCore EXPORT protoCoreTargets ...)` at `protoCore/CMakeLists.txt:209-216` names an export set that no `install(EXPORT protoCoreTargets ...)` ever writes out, so the export is silently discarded; there is no `configure_package_config_file`, no `write_basic_package_version_file`, and no `.pc` file anywhere in the repository. The consequence is visible on this machine: `/usr/local/lib/cmake` does not exist. Every one of the five runtimes therefore hand-rolls `find_library(PROTOCORE_LIBRARY NAMES protoCore ...)` plus `find_path(... protoCore.h ...)` (`protoPython/CMakeLists.txt:18-21`, `protoJS/CMakeLists.txt:23-26`, `protoST/CMakeLists.txt:23-26`, `protoClojure/CMakeLists.txt:24-27`, `protoScala/CMakeLists.txt:24-27`) and accepts **any** `libprotoCore.so` plus **any** `protoCore.h` found under the given prefix, with no version comparison at all.

What that costs, concretely and today: `/usr/local/lib/libprotoCore.so → libprotoCore.so.1 → libprotoCore.so.1.0.0`, and `/usr/local/include/protoCore.h`. The source tree is `2.0.0` / `SOVERSION 2`, and protoCore's own `CHANGELOG.md` records that bump as a deliberate breaking ABI change. `cmake -S protoScala -B build -DPROTO_CORE_PREFIX=/usr/local` succeeds today. It links protoScala 0.2.0 against a protoCore whose object layout it does not share. protoScala makes this worse than a link error: `protoScala/CMakeLists.txt:132-146` decides whether actor mailboxes use `ProtoMPSCQueue` or a CAS'd `ProtoList` by grepping the *text* of whichever `protoCore.h` it found for the substring `newMPSCQueue`. A stale header silently changes the runtime's concurrency implementation.

An installer phase whose central claim is "protoCore is already installed, we depend on it" cannot be built on a discovery mechanism that cannot tell 1.0.0 from 2.0.0. `find_package(protoCore 2.0 CONFIG)` with `SameMajorVersion` rejects 1.0.0 and 3.x and accepts 2.x; the config file additionally asserts that `libprotoCore.so.2` actually exists next to it, which catches the case of a correct config file left behind by an uninstalled or half-replaced library. That is the smallest mechanism that makes the premise checkable, and everything else in this phase — the DEB `Depends`, the RPM `Requires`, protoJS's `preinst`, the WiX `Condition` — is a restatement of the same check in another package manager's vocabulary. Building those without the CMake check first would mean five installers that verify at install time what the build never verified at build time.

The sibling fallback is kept because it is how all six repositories are developed daily, and removing it would break every existing developer build directory. It is demoted rather than deleted: it runs only when no package was found *and* no prefix was named, it prints a `message(WARNING)` saying that no version check was performed and that the mode must not be used to produce a distributable package, it asserts the sibling build actually carries `SOVERSION 2`, and `-DPROTOCORE_REQUIRE_PACKAGE=ON` turns it into a hard error for packaging builds.

*Alternative considered and rejected:* teach each runtime to parse `libprotoCore.so.*` symlink targets itself. That is five copies of a version comparison, in five files, with no `Threads::Threads` propagation, no include-directory propagation and nothing for pkg-config users — exactly the duplication that `find_package` exists to remove.

### D-I2 — Guard the DEB and RPM generators on tool presence.

protoPython (`CMakeLists.txt:1060`), protoST (`:227`), protoClojure (`:200`) and protoJS (`:299`) all set `set(CPACK_GENERATOR "DEB;RPM;TGZ")` unconditionally on non-Apple UNIX. On a host without `rpmbuild` — such as this one — `cpack` fails outright, taking the DEB and the TGZ down with it, because CPack runs the generators in one pass and a missing tool is a fatal error, not a skip. protoCore already solved this, and its solution is the pattern to copy verbatim (`protoCore/CMakeLists.txt:235-247`):

```cmake
else()
    set(CPACK_GENERATOR "TGZ")
    find_program(DPKG_EXECUTABLE dpkg)
    find_program(RPMBUILD_EXECUTABLE rpmbuild)
    if(DPKG_EXECUTABLE)
        list(APPEND CPACK_GENERATOR "DEB")
        message(STATUS "CPack: DEB generator enabled (dpkg found)")
    endif()
    if(RPMBUILD_EXECUTABLE)
        list(APPEND CPACK_GENERATOR "RPM")
        message(STATUS "CPack: RPM generator enabled (rpmbuild found)")
    endif()
endif()
```

The runtimes use repo-prefixed cache variable names (`PROTOPYTHON_DPKG_EXECUTABLE`, and so on) rather than protoCore's bare `DPKG_EXECUTABLE`, because protoPython pulls protoCore in with `add_subdirectory()` in developer mode and two `find_program` calls writing the same cache entry from two projects is a confusing (if harmless) collision. Each branch also prints a `STATUS` line when a generator is *disabled*, so a packaging run that quietly produced fewer artefacts than expected says why.

*Note on scope:* the maintainer's wording named protoPython, protoST and protoClojure. protoJS has the identical defect at `protoJS/CMakeLists.txt:299`, and although D-I4 keeps protoJS's separate pipeline, the broken CPack block is still in its `CMakeLists.txt` and still aborts `cpack`. It is fixed in Task 6 under the same reasoning.

### D-I3 — protoScala gains CPack now.

`protoScala/CMakeLists.txt:175-193` installs `bin/protoscala` and the two documentation files, sets the install RPATH, and stops. `grep -n "CPACK\|CPack"` over the file matches exactly one line — the comment at `:177`, "CPack packaging (.deb / .tar.gz) is completed in Phase 6." The maintainer's scope for this phase is all five runtimes, so the deferral ends here. protoScala is also the cheapest of the five to package: its prelude is embedded into the binary at configure time (`CMakeLists.txt:64-73` reads `lib/prelude.scala` and generates `PreludeSource.cpp`), so the package is one executable plus two documentation files and there is no data-file path that can differ between the build tree and the install tree.

*Alternative considered and rejected:* ship protoScala as a TGZ only. It would be the one runtime of five without a `.deb`, for no reason other than the order the repositories were written in.

### D-I4 — protoJS keeps its own pipeline; only its version floors move.

protoJS is the only repository with real cross-platform installer scaffolding: `packaging/build_deb.sh` (a `dpkg-deb --build` of a staged tree, bypassing CPack entirely), `packaging/templates/linux/control.template`, `preinst.template`, `protoJS.spec.template`, `packaging/templates/macos/preinstall.template` and `packaging/templates/windows/protoJS.wxs.template`. Migrating that to CPack would mean re-expressing the `preinst` dependency check as `CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA`, the WiX conditions as NSIS commands, and the macOS check as a `productbuild` preinstall script — a rewrite whose only output is the same three package files protoJS can already produce. The maintainer's goal is installers that work, not one packaging technology.

What must change is the arithmetic. Every version check in that pipeline is pinned to `1.0.0`: `control.template:5` (`Depends: protocore (>= 1.0.0)`), `preinst.template:4` (`MIN_VERSION="1.0.0"`), `protoJS.spec.template:11` (`Requires: protoCore >= 1.0.0`) and `:20` (`MIN_VERSION="1.0.0"`). Against the actual 2.0.0 source tree those checks do not merely fail to help — they actively certify the stale `/usr/local` 1.0.0 as adequate. They are raised to `>= 2.0.0` with a `< 3.0.0` ceiling (protoCore's `SameMajorVersion` rule, expressed in each package manager's syntax) and, on Linux, backed by a direct check that `libprotoCore.so.2` is present.

Two flaws found while reading those templates rather than assuming them:

1. `packaging/templates/macos/preinstall.template:4-13` accepts protoCore if `pkgutil --pkg-info com.protoCore.pkg` succeeds. protoCore's macOS generator is `DragNDrop` (`protoCore/CMakeLists.txt:234`), which produces a `.dmg` — a disk image, not an installer package. `pkgutil` will never know about it, so that branch can never fire and the template silently depends on its fallback, a bare existence test for `/usr/local/lib/libprotoCore.dylib` that no soname check accompanies. Task 6 fixes the fallback to test the versioned `libprotoCore.2.dylib`; switching protoCore's macOS generator to `productbuild` so a real package id exists is left to the human macOS section, since it cannot be verified here.
2. `packaging/templates/windows/protoJS.wxs.template:13-26` reads `HKLM\SOFTWARE\protoCore\Version` and, failing that, looks for `protoCore.dll` under `[ProgramFiles64Folder]protoCore`. Nothing in protoCore writes that registry key. Task 1 therefore makes protoCore's NSIS installer write `Version`, `Soversion` and `InstallDir` under `HKLM\SOFTWARE\protoCore`, which is the only value the WiX `Condition` can meaningfully test on a platform with no soname.

### D-I5 — protoST's self-location must work on three platforms.

`protoST/src/runtime/STRuntime.cpp:1959-1999` (`discoverStdlibDir()`) probes, in order: `$PROTOST_LIB`; a set of paths derived from the executable's own location; and `<cwd>/lib`. The middle step is written as:

```cpp
    // 2. Derived from the executable location (/proc/self/exe on Linux).
    {
        std::error_code ec;
        fs::path exe = fs::read_symlink("/proc/self/exe", ec);
        if (!ec && !exe.empty()) {
```

On macOS and Windows `/proc/self/exe` does not exist, `read_symlink` sets `ec`, and the entire step is skipped. The installed layout probe `dir.parent_path() / "share" / "protoST" / "lib"` at `:1983` — which is exactly where `install(DIRECTORY lib/ DESTINATION ${CMAKE_INSTALL_DATADIR}/protoST/lib ...)` at `protoST/CMakeLists.txt:189-192` puts the four `.st` modules — is therefore unreachable on those two platforms. A macOS or Windows protoST package installs `bin/protost` and `share/protoST/lib/*.st` and then cannot find its own standard library unless the user sets `PROTOST_LIB` by hand. Shipping that is shipping a broken install, so it is part of this phase's deliverable, not a follow-up.

protoPython already solved this, in `protoPython/src/runtime/main.cpp:38-54`, and that code is what gets ported:

```cpp
static std::string getExecutablePath() {
#ifdef __linux__
    char result[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
    if (count > 0) return std::string(result, count);
#elif defined(_WIN32)
    char result[MAX_PATH];
    DWORD count = GetModuleFileNameA(NULL, result, MAX_PATH);
    if (count > 0) return std::string(result, count);
#elif defined(__APPLE__)
    char result[PATH_MAX];
    uint32_t size = sizeof(result);
    if (_NSGetExecutablePath(result, &size) == 0)
        return std::string(result);
#endif
    return "";
}
```

Task 7 ports it with two deliberate improvements, both noted in the source: the Linux branch rejects `count == sizeof(result)` (a truncated `readlink` result, which protoPython's version would return as a valid path), and the macOS result is passed through `std::filesystem::weakly_canonical` because `_NSGetExecutablePath` may return a path containing `.` or `..` components or an unresolved symlink.

*Alternative considered and rejected:* have the protoST package ship a launcher script that exports `PROTOST_LIB`. It works on Linux and macOS, does not work for a Windows `.exe` started from Explorer, and it makes `protost` un-relocatable — the opposite of what protoPython's design achieves.

### D-I6 — Verify Linux for real; mark macOS and Windows unverified, in writing.

Everything this phase claims about Linux is proved by running it: protoCore is installed into `$S/stage`, each runtime is configured against that prefix with `-DPROTOCORE_REQUIRE_PACKAGE=ON`, built, installed, and each binary is executed and made to do real work (protopy imports `json` from the installed stdlib tree; protost imports `stream` from `share/protoST/lib`); `ldd` is required to show `libprotoCore.so.2` resolving inside the stage prefix. The `.deb` files are built with `cpack -G DEB` and with `packaging/build_deb.sh`, then **extracted** with `dpkg-deb -x` into a second root and smoke-tested from there, which exercises the real package payload paths (`/usr/bin`, `/usr/lib`, `/usr/share`) without `sudo` and without touching the system.

macOS and Windows get configuration, review and honest labelling. `protoST/CMakeLists.txt:224` and `:238` and `protoClojure/CMakeLists.txt:197` and `:208` already carry the comment "(unverified on a Linux build host)"; this phase extends that convention to protoPython, protoScala and protoCore, and adds a **Platform verification status** section to each repository's installation documentation saying which platform was actually exercised and which was not. The final section of this plan lists exactly what a human must run on each of those two hosts.

*Alternative considered and rejected:* cross-building or emulating. There is no macOS or Windows toolchain here, and protoCore's own `docs/INSTALLATION.md:12` already records that a native MSVC build is not expected to work without source changes (`posix_memalign`, `-fno-delete-null-pointer-checks`). Claiming verification we cannot perform is worse than admitting the gap.

### D-I7 — `/usr/local` is left alone, and the version check is the proof.

Nothing in this plan writes to, deletes from or reconfigures `/usr/local`. The stale `1.0.0` stays exactly where it is. It is, however, the best available test fixture for D-I1: it is a prefix containing a real `libprotoCore.so` and a real `protoCore.h` of the wrong major version, which is precisely the situation the new machinery must refuse. Task 8 Step 8 configures protoScala with `-DPROTO_CORE_PREFIX=/usr/local` into a throwaway build directory under `.agent_scratch` and requires a `FATAL_ERROR` naming the version. Before this phase that same command configures successfully.

### Recorded, not asked (facts the maintainer should see)

- **Version skew on this machine.** Installed: `/usr/local/lib/libprotoCore.so.1.0.0` with `libprotoCore.so.1` and `libprotoCore.so` symlinks, plus `/usr/local/include/protoCore.h`. Source: `2.0.0` / `SOVERSION 2`. No `/usr/local/lib/cmake` directory exists at all. After this phase, no runtime will build against that prefix; the machine needs a protoCore 2.x installed (into a prefix of the maintainer's choosing) before any runtime can be packaged here.
- **protoJS's packaging pipeline is a parallel universe to CPack.** `protoJS/CMakeLists.txt:277-314` configures CPack *and* `protoJS/packaging/build_deb.sh` builds a `.deb` by hand, from `build/protojs` (not `build_release/`), with its own `control` and `preinst`. The two produce differently-named packages (`protojs` from CPack, `protoJS` from the template at `control.template:1`) with different dependency metadata. D-I4 keeps both; the maintainer should know that two `.deb` files with two package names can be produced from one repository, and may want to retire one of them in a later phase.
- **Runtime data-file exposure is very uneven.** protoScala embeds its prelude at configure time (`CMakeLists.txt:64-73`) and protoClojure and protoJS compile everything in, so those three have no data-file path that can differ between build tree and install tree. protoST ships four `.st` files to `share/protoST/lib`. protoPython ships the entire `lib/python3.14` pure-Python standard library as real files to `<prefix>/lib/protoPython/python3.14` (`CMakeLists.txt:1039-1043`), located at runtime through a *relative* `STDLIB_PATH` resolved against the executable's own directory (`src/runtime/CMakeLists.txt:10-21`, `src/runtime/main.cpp:125-140`) — the best design of the family, and also the largest payload and the one whose correctness the package must actually demonstrate.
- **The DEB/RPM package-name asymmetry is currently an accident.** No repository sets `CPACK_DEBIAN_PACKAGE_NAME` or `CPACK_RPM_PACKAGE_NAME`, so the names come from CPack's generator defaults: the DEB generator lower-cases `CPACK_PACKAGE_NAME`, the RPM generator does not. Four downstream repositories have hand-written comments explaining that this is so (for example `protoST/CMakeLists.txt:229-230`), and protoJS's `preinst.template:7` loops over both spellings to survive it. This plan **pins the names explicitly** (`protocore` for DEB, `protoCore` for RPM, and likewise per runtime) so that behaviour stops being a generator implementation detail. The observable names do not change.
- **protoCore's `INSTALL_INTERFACE` hardcodes `include`.** `protoCore/CMakeLists.txt:72-75` writes `$<INSTALL_INTERFACE:include>` while every install destination uses `GNUInstallDirs`. On a distribution where `CMAKE_INSTALL_INCLUDEDIR` is not `include`, the exported target would advertise a directory that does not exist. Task 1 Step 3 changes it to `$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>`; this only becomes observable once `install(EXPORT)` exists, which is why it belongs to this phase.
- **`package_protocore_only` remains wrong and remains out of scope.** The custom target at `protoCore/CMakeLists.txt:262-272` tars up `$<TARGET_FILE:protoCore>` alone — the `libprotoCore.so.2.0.0` file with neither the `.so.2` soname link nor the `.so` link — and writes it to the same filename the CPack TGZ generator uses. `docs/INSTALLATION.md:163` already documents both problems. This plan does not touch it; it is not part of any installer path. The maintainer may want it deleted.
- **protoCore does not declare a `Provides:` for its shared library.** A conventional Debian shared-library package would provide `libprotocore2`. This plan keeps the single `protocore` package name that all five runtimes already depend on; splitting into `libprotocore2` plus `libprotocore-dev` is a larger packaging decision and is not taken here.
- **protoPython's own `SOVERSION` is `0`** (`src/library/CMakeLists.txt:67-70`) while its project version is `1.0.0`. It is the only runtime that installs a shared library of its own, and its package therefore carries an ABI-unversioned `libprotoPython.so.0`. Nothing in this phase changes it; it is noted because it is the one place where a second soname enters the installers.

---

## File Structure

**protoCore** (`/home/gamarino/Documentos/proyectos/protoCore`):

| File | Change | Responsibility |
|---|---|---|
| `cmake/protoCoreConfig.cmake.in` | **Create** | The package config template: version, soversion, include/lib dirs, `find_dependency(Threads)`, the ABI assertion, and the include of `protoCoreTargets.cmake`. |
| `cmake/protoCore.pc.in` | **Create** | pkg-config metadata, including a custom `soversion` variable. |
| `CMakeLists.txt` | Modify | `PROTOCORE_ABI_SOVERSION` variable (`:62-68`); `$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>` (`:74`); `install(EXPORT)`, package config generation and `.pc` install (after `:216`); explicit CPack package names and the NSIS registry commands (`:219-247`). |
| `docs/INSTALLATION.md` | Modify | Replace `:85` ("The install rules export no CMake package configuration file"); new "Consuming protoCore from CMake" and "Platform verification status" sections. |
| `CHANGELOG.md` | Modify | `## [Unreleased]` entry. |

**protoPython** (`/home/gamarino/Documentos/proyectos/protoPython`):

| File | Change | Responsibility |
|---|---|---|
| `CMakeLists.txt` | Modify | protoCore discovery (`:14-42`); CPack generator guard and versioned dependencies (`:1056-1074`). |
| `src/compiler/CMakeLists.txt` | Modify | `if(DEFINED PROTO_CORE_PREFIX)` → `if(PROTOPYTHON_PROTOCORE_EXTERNAL)` (`:16`). |
| `docs/INSTALLATION.md` | Modify | New protoCore requirement and platform verification status. |

**protoST** (`/home/gamarino/Documentos/proyectos/protoST`):

| File | Change | Responsibility |
|---|---|---|
| `CMakeLists.txt` | Modify | protoCore discovery (`:21-52`); CPack generator guard and versioned dependencies (`:222-242`). |
| `src/runtime/STRuntime.cpp` | Modify | Platform includes (near `:24-40`); `executablePath()` and the rewritten step 2 of `discoverStdlibDir()` (`:1946-1999`). |
| `docs/INSTALLATION.md` | **Create** | Build, install, package, stdlib discovery, platform verification status. |

**protoClojure** (`/home/gamarino/Documentos/proyectos/protoClojure`):

| File | Change | Responsibility |
|---|---|---|
| `CMakeLists.txt` | Modify | protoCore discovery (`:19-47`); CPack generator guard and versioned dependencies (`:192-213`). |
| `docs/INSTALLATION.md` | **Create** | Build, install, package, platform verification status. |

**protoScala** (`/home/gamarino/Documentos/proyectos/protoScala`):

| File | Change | Responsibility |
|---|---|---|
| `CMakeLists.txt` | Modify | protoCore discovery (`:19-47`); the PMQ probe's include source (`:132-139`); a whole new CPack block after `:193`. |
| `docs/INSTALLATION.md` | **Create** | Build, install, package, platform verification status. |
| `docs/DECISIONS-LOG.md` | Modify | Record D-I1..D-I7 as agent-taken decisions (approved by the maintainer on 2026-09-23). |

**protoJS** (`/home/gamarino/Documentos/proyectos/protoJS`):

| File | Change | Responsibility |
|---|---|---|
| `CMakeLists.txt` | Modify | protoCore discovery (`:14-51`); CPack generator guard and versioned dependencies (`:295-312`). |
| `packaging/templates/linux/control.template` | Modify | `Depends:` floor and ceiling (`:5`). |
| `packaging/templates/linux/preinst.template` | Modify | `MIN_VERSION` and the new ceiling plus soname check (`:4-27`). |
| `packaging/templates/linux/protoJS.spec.template` | Modify | `Requires:` (`:11`) and the `%pre` scriptlet (`:19-38`). |
| `packaging/templates/macos/preinstall.template` | Modify | Versioned dylib check (`:4-19`). |
| `packaging/templates/windows/protoJS.wxs.template` | Modify | Registry search for `Soversion` and the blocking condition (`:11-26`). |
| `packaging/build_deb.sh` | Modify | Build from `build_release/`, accept an explicit binary path, fail on a wrong soname (`:8-23`). |
| `docs/INSTALLATION.md` | Modify | protoCore 2.x requirement and platform verification status. |

---

## Interface contract: the protoCore CMake package

Written once here because six tasks consume it, and a mismatch between any two of them is a bug. Every name below is used verbatim in Tasks 1 through 8.

**What Task 1 produces**, installed under the prefix:

| Path (relative to the prefix) | Content |
|---|---|
| `${CMAKE_INSTALL_LIBDIR}/cmake/protoCore/protoCoreConfig.cmake` | The package config, from `cmake/protoCoreConfig.cmake.in`. |
| `${CMAKE_INSTALL_LIBDIR}/cmake/protoCore/protoCoreConfigVersion.cmake` | `write_basic_package_version_file`, `COMPATIBILITY SameMajorVersion`. |
| `${CMAKE_INSTALL_LIBDIR}/cmake/protoCore/protoCoreTargets.cmake` (+ a per-configuration file) | `install(EXPORT protoCoreTargets NAMESPACE protoCore:: ...)`. |
| `${CMAKE_INSTALL_LIBDIR}/pkgconfig/protoCore.pc` | pkg-config metadata. |
| `${CMAKE_INSTALL_LIBDIR}/libprotoCore.so`, `.so.2`, `.so.2.0.0` | The library (unchanged from today). |
| `${CMAKE_INSTALL_INCLUDEDIR}/protoCore.h` | The public header (unchanged from today). |

**What a consumer gets after `find_package(protoCore 2.0 CONFIG)` succeeds:**

| Name | Kind | Value |
|---|---|---|
| `protoCore::protoCore` | imported `SHARED` target | The library, with `INTERFACE_INCLUDE_DIRECTORIES` pointing at the installed include dir and `INTERFACE_LINK_LIBRARIES` carrying `Threads::Threads`. |
| `protoCore_VERSION` | variable | `2.0.0` |
| `protoCore_SOVERSION` | variable | `2` |
| `protoCore_INCLUDE_DIR` | variable | absolute path of the directory holding `protoCore.h` |
| `protoCore_LIB_DIR` | variable | absolute path of the directory holding the shared library |
| `protoCore_DIR` | variable (set by CMake) | the directory the config was found in |

**Names every consumer defines, identically** (protoPython, protoST, protoClojure, protoScala, protoJS):

| Name | Kind | Value | Used for |
|---|---|---|---|
| `PROTOCORE_MIN_VERSION` | variable | `2.0` | the `find_package` version argument |
| `PROTOCORE_MIN_VERSION_FULL` | variable | `2.0.0` | the DEB/RPM dependency floor |
| `PROTOCORE_NEXT_MAJOR` | variable | `3.0.0` | the DEB/RPM dependency ceiling |
| `PROTOCORE_ABI_SOVERSION` | variable | `2` | the consumer-side ABI assertion and the soname file check |
| `PROTOCORE_REQUIRE_PACKAGE` | `option`, default `OFF` | — | `ON` forbids the sibling fallback; every packaging build sets it |
| `PROTOCORE_LIBRARY` | variable | `protoCore::protoCore` in package mode, an absolute `.so` path in fallback mode | what `target_link_libraries` receives (protoST, protoClojure, protoScala, protoJS) |
| `PROTOCORE_INCLUDE_DIRS` | variable | `${protoCore_INCLUDE_DIR}` in package mode, `${PROTOCORE_DIR};${PROTOCORE_DIR}/headers` in fallback mode | the existing `include_directories()` calls, unchanged |

protoPython is the exception on the last two rows: it links the **unqualified** target name `protoCore` in three places (`src/library/CMakeLists.txt:64`, `src/compiler/CMakeLists.txt:41`, `CMakeLists.txt:961` and `:1003`) and uses `$<TARGET_FILE_DIR:protoCore>` in two generated-file expressions (`src/compiler/CMakeLists.txt:19` and `:28`). It therefore promotes the imported target to global and aliases it to `protoCore`, so none of those five references changes, and it defines `PROTOPYTHON_PROTOCORE_EXTERNAL` (TRUE when protoCore comes from outside the build) and `PROTOPYTHON_PROTOCORE_INCLUDE_DIR` as it does today.

---

### Task 1: protoCore — the CMake package config, the pkg-config file and the ABI assertion

**Files:**
- Create: `protoCore/cmake/protoCoreConfig.cmake.in`
- Create: `protoCore/cmake/protoCore.pc.in`
- Modify: `protoCore/CMakeLists.txt` (lines `62-68`, `74`, after `216`, and `219-247`)
- Modify: `protoCore/docs/INSTALLATION.md` (line `85`, and two new sections)
- Modify: `protoCore/CHANGELOG.md`

**Interfaces:**
- Consumes: nothing. This is the first task and has no dependency on the others.
- Produces: everything in the "Interface contract" section above — `protoCore::protoCore`, `protoCore_VERSION`, `protoCore_SOVERSION`, `protoCore_INCLUDE_DIR`, `protoCore_LIB_DIR`, `protoCore.pc` with its `soversion` variable, and the `HKLM\SOFTWARE\protoCore` registry values written by the NSIS installer.

- [ ] **Step 1: Branch and scratch directory**

```bash
W=/home/gamarino/Documentos/proyectos
S=$W/.agent_scratch/phase-i-installers
mkdir -p $S
git -C $W/protoCore status --short
git -C $W/protoCore switch -c feature/phase-i-installers
```
Expected: `git status --short` is empty or lists only files another agent left behind. If anything under `CMakeLists.txt`, `cmake/` or `docs/INSTALLATION.md` is already modified, **stop and ask** — protoCore is shared with the P2 plan.

- [ ] **Step 2: Make the ABI version a single named variable**

`protoCore/CMakeLists.txt:63-68` currently reads:

```cmake
set_target_properties(protoCore PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 2
    OUTPUT_NAME protoCore
    PUBLIC_HEADER "headers/protoCore.h"
)
```

Replace it with:

```cmake
# The ABI version of libprotoCore. It is the single source of truth for the
# soname, for the generated CMake package config and for protoCore.pc, so a
# consumer's check and the file on disk can never disagree. Bump it only for a
# breaking ABI change, as 1 -> 2 was (see CHANGELOG.md).
set(PROTOCORE_ABI_SOVERSION 2)

set_target_properties(protoCore PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROTOCORE_ABI_SOVERSION}
    OUTPUT_NAME protoCore
    PUBLIC_HEADER "headers/protoCore.h"
)
```

- [ ] **Step 3: Make the exported include directory honour `GNUInstallDirs`**

`protoCore/CMakeLists.txt:72-75` currently reads:

```cmake
target_include_directories(protoCore PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/headers>
    $<INSTALL_INTERFACE:include>
)
```

Replace `$<INSTALL_INTERFACE:include>` with the `GNUInstallDirs` variable that the install rule at `:215` already uses, so the exported target advertises the directory the header is actually installed to:

```cmake
target_include_directories(protoCore PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/headers>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
```

- [ ] **Step 4: Write the package config template**

Create `protoCore/cmake/protoCoreConfig.cmake.in`:

```cmake
# protoCoreConfig.cmake - generated from cmake/protoCoreConfig.cmake.in.
#
# Consumed by:  find_package(protoCore 2.0 REQUIRED CONFIG)
#
# Provides:
#   protoCore::protoCore    imported SHARED library (include dirs + Threads)
#   protoCore_VERSION       full version, for example 2.0.0
#   protoCore_SOVERSION     ABI version of the shared library, for example 2
#   protoCore_INCLUDE_DIR   directory holding protoCore.h
#   protoCore_LIB_DIR       directory holding the shared library
#
# Version compatibility is SameMajorVersion (protoCoreConfigVersion.cmake):
# a request for 2.0 is satisfied by any 2.x and rejected by 1.x and 3.x,
# because protoCore's major version and its soname are bumped together.

@PACKAGE_INIT@

set(protoCore_VERSION   "@PROJECT_VERSION@")
set(protoCore_SOVERSION "@PROTOCORE_ABI_SOVERSION@")

set_and_check(protoCore_INCLUDE_DIR "@PACKAGE_CMAKE_INSTALL_INCLUDEDIR@")
set_and_check(protoCore_LIB_DIR     "@PACKAGE_CMAKE_INSTALL_LIBDIR@")

include(CMakeFindDependencyMacro)
find_dependency(Threads)

# ABI assertion. The version file above compares numbers written into a text
# file; this compares them against the library that is actually on disk. It
# catches a prefix whose CMake package files survived an uninstall, a partial
# reinstall, or a manual copy of headers without the matching library.
if(APPLE)
    set(_protoCore_abi_file "${protoCore_LIB_DIR}/libprotoCore.${protoCore_SOVERSION}.dylib")
elseif(WIN32)
    # Windows shared libraries carry no soname; the import library pins the ABI.
    set(_protoCore_abi_file "")
else()
    set(_protoCore_abi_file "${protoCore_LIB_DIR}/libprotoCore.so.${protoCore_SOVERSION}")
endif()

if(_protoCore_abi_file AND NOT EXISTS "${_protoCore_abi_file}")
    set(protoCore_FOUND FALSE)
    set(protoCore_NOT_FOUND_MESSAGE
        "protoCore ${protoCore_VERSION} package configuration was found in "
        "${CMAKE_CURRENT_LIST_DIR}, but the ABI library it declares "
        "(${_protoCore_abi_file}) does not exist. That prefix holds a "
        "mismatched or partially removed protoCore: reinstall protoCore "
        "${protoCore_VERSION} (SOVERSION ${protoCore_SOVERSION}) into it.")
    unset(_protoCore_abi_file)
    return()
endif()
unset(_protoCore_abi_file)

include("${CMAKE_CURRENT_LIST_DIR}/protoCoreTargets.cmake")

check_required_components(protoCore)
```

- [ ] **Step 5: Write the pkg-config template**

Create `protoCore/cmake/protoCore.pc.in`. It is configured with `@ONLY`, so the `${...}` references below survive into the generated file and are expanded by pkg-config, not by CMake:

```
prefix=@CMAKE_INSTALL_PREFIX@
exec_prefix=${prefix}
libdir=@CMAKE_INSTALL_FULL_LIBDIR@
includedir=@CMAKE_INSTALL_FULL_INCLUDEDIR@
soversion=@PROTOCORE_ABI_SOVERSION@

Name: protoCore
Description: @PROJECT_DESCRIPTION@
URL: @PROJECT_HOMEPAGE_URL@
Version: @PROJECT_VERSION@
Libs: -L${libdir} -lprotoCore
Libs.private: -lpthread
Cflags: -I${includedir} -std=c++20
```

A consumer that is not a CMake project checks the ABI with `pkg-config --variable=soversion protoCore`, which is why `soversion` is declared as a pkg-config variable rather than only appearing in `Version`.

- [ ] **Step 6: Emit the export set, the config files and the `.pc`**

In `protoCore/CMakeLists.txt`, immediately after the `install(TARGETS ...)` block that ends at `:216` and before the `# 7. Packaging configuration (CPack)` comment at `:218`, insert:

```cmake
# --- CMake package configuration -------------------------------------------
# Without these rules the EXPORT keyword above names an export set that is
# never written out, so no protoCoreConfig.cmake exists and no consumer can
# check protoCore's version or ABI before linking against it.
include(CMakePackageConfigHelpers)

set(PROTOCORE_CMAKE_CONFIG_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/protoCore")

install(EXPORT protoCoreTargets
    FILE protoCoreTargets.cmake
    NAMESPACE protoCore::
    DESTINATION "${PROTOCORE_CMAKE_CONFIG_INSTALL_DIR}"
    COMPONENT ${PROTOCORE_INSTALL_COMPONENT}
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/protoCoreConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/protoCoreConfig.cmake"
    INSTALL_DESTINATION "${PROTOCORE_CMAKE_CONFIG_INSTALL_DIR}"
    PATH_VARS CMAKE_INSTALL_INCLUDEDIR CMAKE_INSTALL_LIBDIR
)

# SameMajorVersion: find_package(protoCore 2.0) accepts any 2.x and rejects
# 1.x and 3.x, matching the rule that protoCore's major version and its
# soname move together.
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/protoCoreConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/protoCoreConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/protoCoreConfigVersion.cmake"
    DESTINATION "${PROTOCORE_CMAKE_CONFIG_INSTALL_DIR}"
    COMPONENT ${PROTOCORE_INSTALL_COMPONENT}
)

# pkg-config metadata, for consumers that are not CMake projects.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/protoCore.pc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/protoCore.pc"
    @ONLY
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/protoCore.pc"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig"
    COMPONENT ${PROTOCORE_INSTALL_COMPONENT}
)
```

All four install rules use `${PROTOCORE_INSTALL_COMPONENT}` (set to `"protoCore"` at `:208`), so `CPACK_COMPONENTS_ALL protoCore` at `:250` packages them without further change.

- [ ] **Step 7: Pin the CPack package names and give the NSIS installer a registry footprint**

In the CPack block, after `set(CPACK_PACKAGE_NAME "protoCore")` at `:219`, add:

```cmake
# Pinned explicitly rather than left to each generator's default casing. The
# DEB generator lower-cases CPACK_PACKAGE_NAME and the RPM generator does not;
# four downstream repositories and protoJS's preinst script depend on the
# resulting names, so they are stated here instead of being inferred.
set(CPACK_DEBIAN_PACKAGE_NAME "protocore")
set(CPACK_RPM_PACKAGE_NAME    "protoCore")
```

And replace the `if(WIN32)` branch at `:231-232` with:

```cmake
if(WIN32)
    set(CPACK_GENERATOR "ZIP;NSIS")
    # A Windows DLL carries no soname, so a dependent installer has nothing on
    # disk to inspect. Record the version and the ABI in the registry instead:
    # protoJS's WiX template reads exactly these two values
    # (protoJS/packaging/templates/windows/protoJS.wxs.template).
    # UNVERIFIED: configured and reviewed, never run - no Windows host (D-I6).
    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
        WriteRegStr HKLM 'SOFTWARE\\protoCore' 'Version' '${PROJECT_VERSION}'
        WriteRegStr HKLM 'SOFTWARE\\protoCore' 'Soversion' '${PROTOCORE_ABI_SOVERSION}'
        WriteRegStr HKLM 'SOFTWARE\\protoCore' 'InstallDir' '$INSTDIR'
    ")
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "
        DeleteRegValue HKLM 'SOFTWARE\\protoCore' 'Version'
        DeleteRegValue HKLM 'SOFTWARE\\protoCore' 'Soversion'
        DeleteRegValue HKLM 'SOFTWARE\\protoCore' 'InstallDir'
        DeleteRegKey /ifempty HKLM 'SOFTWARE\\protoCore'
    ")
elseif(APPLE)
```

(The `elseif(APPLE)` at `:233` and everything after it is unchanged. In a CMake quoted argument `\\` produces one backslash, which is what NSIS needs; NSIS accepts single-quoted strings, which avoids escaping the double quotes.)

- [ ] **Step 8: Configure, build and install into the scratch prefix**

```bash
W=/home/gamarino/Documentos/proyectos
S=$W/.agent_scratch/phase-i-installers
STAGE=$S/stage
rm -rf $STAGE && mkdir -p $STAGE
cmake -S $W/protoCore -B $W/protoCore/build_release \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$STAGE
cmake --build $W/protoCore/build_release -j4 --target protoCore
cmake --install $W/protoCore/build_release --component protoCore
find $STAGE -type f -o -type l | sort | tee $S/protocore-installed-files.txt
```
Expected in `$S/protocore-installed-files.txt`: `include/protoCore.h`; `lib/libprotoCore.so`, `lib/libprotoCore.so.2`, `lib/libprotoCore.so.2.0.0`; `lib/cmake/protoCore/protoCoreConfig.cmake`, `protoCoreConfigVersion.cmake`, `protoCoreTargets.cmake`, `protoCoreTargets-release.cmake`; `lib/pkgconfig/protoCore.pc`.

- [ ] **Step 9: Prove the package answers the three questions a consumer asks**

```bash
S=/home/gamarino/Documentos/proyectos/.agent_scratch/phase-i-installers
STAGE=$S/stage
PKG_CONFIG_PATH=$STAGE/lib/pkgconfig pkg-config --modversion protoCore
PKG_CONFIG_PATH=$STAGE/lib/pkgconfig pkg-config --variable=soversion protoCore
mkdir -p $S/probe && cat > $S/probe/CMakeLists.txt <<'EOF'
cmake_minimum_required(VERSION 3.16)
project(protoCoreProbe LANGUAGES CXX)
find_package(protoCore 2.0 REQUIRED CONFIG)
message(STATUS "PROBE version=${protoCore_VERSION} soversion=${protoCore_SOVERSION}")
message(STATUS "PROBE include=${protoCore_INCLUDE_DIR}")
get_target_property(_loc protoCore::protoCore IMPORTED_LOCATION_RELEASE)
message(STATUS "PROBE location=${_loc}")
EOF
cmake -S $S/probe -B $S/probe/build -DCMAKE_PREFIX_PATH=$STAGE 2>&1 | grep PROBE
cmake -S $S/probe -B $S/probe/build-bad -DCMAKE_PREFIX_PATH=$STAGE \
    -DCMAKE_PROJECT_INCLUDE_BEFORE=/dev/null 2>&1 | tail -3
```
Expected: `2.0.0`; `2`; `PROBE version=2.0.0 soversion=2`; `PROBE include=$STAGE/include`; `PROBE location=$STAGE/lib/libprotoCore.so.2.0.0`.

- [ ] **Step 10: Negative control — the version check must be able to fail**

```bash
S=/home/gamarino/Documentos/proyectos/.agent_scratch/phase-i-installers
sed -i 's/find_package(protoCore 2.0 REQUIRED CONFIG)/find_package(protoCore 3.0 REQUIRED CONFIG)/' $S/probe/CMakeLists.txt
cmake -S $S/probe -B $S/probe/build-v3 -DCMAKE_PREFIX_PATH=$S/stage 2>&1 | tail -6
sed -i 's/find_package(protoCore 3.0 REQUIRED CONFIG)/find_package(protoCore 2.0 REQUIRED CONFIG)/' $S/probe/CMakeLists.txt
```
Expected: a CMake error naming `protoCoreConfigVersion.cmake` and reporting the found version `2.0.0` as incompatible with the requested `3.0`. A success here means `SameMajorVersion` is not in effect and Step 6 is wrong.

- [ ] **Step 11: Update `docs/INSTALLATION.md`**

Replace line `85` — "The install rules export no CMake package configuration file, so consumers locate protoCore with `find_library` and `find_path` (or `-I<prefix>/include -L<prefix>/lib -lprotoCore`)." — with a new section:

```markdown
## Consuming protoCore from CMake

The install rules export a CMake package configuration, so a consumer asks for
protoCore by name and version rather than searching for files:

```cmake
find_package(protoCore 2.0 REQUIRED CONFIG)
target_link_libraries(my_target PRIVATE protoCore::protoCore)
```

`find_package` uses `CMAKE_PREFIX_PATH` to find a non-default prefix:

```bash
cmake -B build -S . -DCMAKE_PREFIX_PATH=$HOME/.local
```

The package provides:

| Name | Meaning |
|------|---------|
| `protoCore::protoCore` | The imported shared library, carrying the include directory and `Threads::Threads` |
| `protoCore_VERSION` | Full version, for example `2.0.0` |
| `protoCore_SOVERSION` | ABI version of the shared library, for example `2` |
| `protoCore_INCLUDE_DIR` | Directory holding `protoCore.h` |
| `protoCore_LIB_DIR` | Directory holding the shared library |

Version compatibility is `SameMajorVersion`: a request for `2.0` is satisfied by
any `2.x` and refused for `1.x` and `3.x`, because protoCore's major version and
its soname are bumped together. The configuration additionally checks that
`libprotoCore.so.2` (`libprotoCore.2.dylib` on macOS) exists beside it, so a
prefix whose CMake files outlived its library fails with a message rather than a
link error.

For consumers that are not CMake projects, `lib/pkgconfig/protoCore.pc` is
installed:

```bash
pkg-config --cflags --libs protoCore
pkg-config --variable=soversion protoCore   # 2
```

## Platform verification status

| Platform | Packaging | Status |
|----------|-----------|--------|
| Linux | TGZ, DEB (needs `dpkg`), RPM (needs `rpmbuild`) | Built, installed to a scratch prefix and smoke-tested |
| macOS | TGZ, DragNDrop | Configured and reviewed, **never built** — no macOS host |
| Windows | ZIP, NSIS (writes `HKLM\SOFTWARE\protoCore` `Version`, `Soversion`, `InstallDir`) | Configured and reviewed, **never built** — no Windows host |
```

Also correct `docs/INSTALLATION.md:174`, which states that protoJS looks in `../protoCore/build` and `../protoCore/build_check`: the real search order is `build_release`, then `build`, then `build_check` (`protoJS/CMakeLists.txt:38`), and after Task 6 that path is a fallback behind `find_package`.

- [ ] **Step 12: CHANGELOG and commit**

Add to `protoCore/CHANGELOG.md`, above the `[2.0.0]` entry:

```markdown
## [Unreleased]

### Added
- CMake package configuration: `install(EXPORT protoCoreTargets)` with the
  namespace `protoCore::`, `protoCoreConfig.cmake` and a
  `SameMajorVersion` `protoCoreConfigVersion.cmake`. Consumers now use
  `find_package(protoCore 2.0 REQUIRED CONFIG)` and link
  `protoCore::protoCore`. The configuration also asserts that the library
  matching `SOVERSION` is present in the prefix.
- `lib/pkgconfig/protoCore.pc`, including a `soversion` pkg-config variable.
- The NSIS installer records `Version`, `Soversion` and `InstallDir` under
  `HKLM\SOFTWARE\protoCore`, so dependent Windows installers have something
  to test. Configured but unverified: no Windows host.

### Changed
- `SOVERSION` is now derived from the new `PROTOCORE_ABI_SOVERSION` variable,
  which is also what the package configuration and `protoCore.pc` report.
- The exported interface include directory uses `CMAKE_INSTALL_INCLUDEDIR`
  instead of the hardcoded `include`.
- `CPACK_DEBIAN_PACKAGE_NAME` (`protocore`) and `CPACK_RPM_PACKAGE_NAME`
  (`protoCore`) are set explicitly instead of relying on each generator's
  default casing. The resulting package names are unchanged.
```

```bash
W=/home/gamarino/Documentos/proyectos
git -C $W/protoCore add CMakeLists.txt cmake/protoCoreConfig.cmake.in cmake/protoCore.pc.in docs/INSTALLATION.md CHANGELOG.md
git -C $W/protoCore commit -m "$(cat <<'EOF'
Emit a CMake package configuration and pkg-config file for protoCore

install(TARGETS ... EXPORT protoCoreTargets) named an export set that was
never written out, so no protoCoreConfig.cmake existed and no consumer could
check protoCore's version or ABI before linking. Add install(EXPORT),
configure_package_config_file, a SameMajorVersion version file, an explicit
SOVERSION assertion and protoCore.pc, and drive the soname from one variable.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

**Done-when:**

```bash
S=/home/gamarino/Documentos/proyectos/.agent_scratch/phase-i-installers
test -f $S/stage/lib/cmake/protoCore/protoCoreConfig.cmake \
  && test -f $S/stage/lib/pkgconfig/protoCore.pc \
  && [ "$(PKG_CONFIG_PATH=$S/stage/lib/pkgconfig pkg-config --variable=soversion protoCore)" = "2" ] \
  && cmake -S $S/probe -B $S/probe/build-dw -DCMAKE_PREFIX_PATH=$S/stage 2>&1 | grep -q "PROBE soversion=2\|soversion=2" \
  && echo "TASK 1 DONE"
```
prints `TASK 1 DONE`.

---

### Task 2: protoPython — `find_package`, guarded generators, versioned dependency

**Files:**
- Modify: `protoPython/CMakeLists.txt` (`:14-42` and `:1056-1074`)
- Modify: `protoPython/src/compiler/CMakeLists.txt` (`:16`)
- Modify: `protoPython/docs/INSTALLATION.md`

**Interfaces:**
- Consumes: `protoCore::protoCore`, `protoCore_VERSION`, `protoCore_SOVERSION`, `protoCore_INCLUDE_DIR` from Task 1.
- Produces: the unqualified alias target `protoCore` (so `src/library/CMakeLists.txt:64`, `src/compiler/CMakeLists.txt:41`, `CMakeLists.txt:961` and `:1003` and the two `$<TARGET_FILE_DIR:protoCore>` expressions are untouched); `PROTOPYTHON_PROTOCORE_EXTERNAL` (`TRUE` when protoCore comes from outside this build); `PROTOPYTHON_PROTOCORE_INCLUDE_DIR` (unchanged meaning); a `protopython` DEB depending on `protocore (>= 2.0.0), protocore (<< 3.0.0)`.

- [ ] **Step 1: Branch**

```bash
W=/home/gamarino/Documentos/proyectos
git -C $W/protoPython status --short
git -C $W/protoPython switch -c feature/phase-i-installers
```

- [ ] **Step 2: Replace the protoCore discovery block**

Replace `protoPython/CMakeLists.txt:14-42` (the comment at `:14-16` through the `endif()` at `:42`) with:

```cmake
# --- protoCore -----------------------------------------------------------------
# Resolution order (Phase I, decision D-I1):
#   1. An installed protoCore CMake package: find_package(protoCore 2.0 CONFIG).
#      This is the only mode that checks protoCore's version and ABI, so it is
#      the only mode a distributable package may be built in. Point at a prefix
#      outside the default search path with -DPROTO_CORE_PREFIX=<prefix> or
#      -DCMAKE_PREFIX_PATH=<prefix>.
#   2. Developer fallback: build protoCore from the sibling source tree
#      ../protoCore with add_subdirectory(). Used only when no installed package
#      was found and no prefix was named; refused by -DPROTOCORE_REQUIRE_PACKAGE=ON.
set(PROTOCORE_MIN_VERSION      "2.0")
set(PROTOCORE_MIN_VERSION_FULL "2.0.0")
set(PROTOCORE_NEXT_MAJOR       "3.0.0")
set(PROTOCORE_ABI_SOVERSION    "2")
option(PROTOCORE_REQUIRE_PACKAGE
    "Require an installed protoCore CMake package; never fall back to ../protoCore" OFF)

if(DEFINED PROTO_CORE_PREFIX)
    list(APPEND CMAKE_PREFIX_PATH "${PROTO_CORE_PREFIX}")
endif()

find_package(protoCore ${PROTOCORE_MIN_VERSION} QUIET CONFIG)

if(protoCore_FOUND)
    if(NOT protoCore_SOVERSION STREQUAL PROTOCORE_ABI_SOVERSION)
        message(FATAL_ERROR
            "protoCore package at ${protoCore_DIR} declares SOVERSION "
            "${protoCore_SOVERSION}; protoPython is built against SOVERSION "
            "${PROTOCORE_ABI_SOVERSION}. Install a matching protoCore.")
    endif()
    # This project links the unqualified target name `protoCore` and uses
    # $<TARGET_FILE_DIR:protoCore> (src/library/CMakeLists.txt:64,
    # src/compiler/CMakeLists.txt:19, :28, :41). Promote the imported target to
    # global scope and alias it, so none of those references has to change.
    if(NOT TARGET protoCore)
        set_target_properties(protoCore::protoCore PROPERTIES IMPORTED_GLOBAL TRUE)
        add_library(protoCore ALIAS protoCore::protoCore)
    endif()
    set(PROTOPYTHON_PROTOCORE_INCLUDE_DIR "${protoCore_INCLUDE_DIR}")
    set(PROTOPYTHON_PROTOCORE_EXTERNAL TRUE)
    message(STATUS "protoPython: installed protoCore ${protoCore_VERSION} "
                   "(SOVERSION ${protoCore_SOVERSION}) from ${protoCore_DIR}")
elseif(DEFINED PROTO_CORE_PREFIX OR PROTOCORE_REQUIRE_PACKAGE)
    # A generator expression is not evaluated by message(), so the location is
    # built as a plain string first.
    set(_protoCore_where "")
    if(DEFINED PROTO_CORE_PREFIX)
        set(_protoCore_where " under ${PROTO_CORE_PREFIX}")
    endif()
    message(FATAL_ERROR
        "No protoCore >= ${PROTOCORE_MIN_VERSION_FULL} CMake package was found"
        "${_protoCore_where}.\n"
        "  A prefix holding only libprotoCore and protoCore.h is no longer "
        "accepted: without lib/cmake/protoCore/protoCoreConfig.cmake there is "
        "no way to tell protoCore 1.x from 2.x, and linking the wrong one is "
        "silent. Build and install protoCore ${PROTOCORE_MIN_VERSION_FULL} or "
        "later:\n"
        "    cmake -S ../protoCore -B ../protoCore/build_release "
        "-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=<prefix>\n"
        "    cmake --build ../protoCore/build_release --target protoCore\n"
        "    cmake --install ../protoCore/build_release --component protoCore")
    unset(_protoCore_where)
else()
    set(PROTOCORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../protoCore")
    if(NOT EXISTS "${PROTOCORE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "No installed protoCore package and no sibling source tree at "
            "${PROTOCORE_DIR}. Install protoCore ${PROTOCORE_MIN_VERSION_FULL} "
            "or pass -DPROTO_CORE_PREFIX=<prefix>.")
    endif()
    message(WARNING
        "protoPython: no installed protoCore package found; building protoCore "
        "from ${PROTOCORE_DIR}. This developer mode performs no package version "
        "check and must not be used to produce a distributable package "
        "(configure with -DPROTOCORE_REQUIRE_PACKAGE=ON to forbid it).")
    if(NOT TARGET protoCore)
        add_subdirectory("${PROTOCORE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/protoCore")
    endif()
    get_target_property(_protoCore_sibling_soversion protoCore SOVERSION)
    if(NOT _protoCore_sibling_soversion STREQUAL PROTOCORE_ABI_SOVERSION)
        message(FATAL_ERROR
            "The sibling protoCore at ${PROTOCORE_DIR} has SOVERSION "
            "${_protoCore_sibling_soversion}; protoPython is built against "
            "SOVERSION ${PROTOCORE_ABI_SOVERSION}.")
    endif()
    unset(_protoCore_sibling_soversion)
    get_filename_component(PROTOPYTHON_PROTOCORE_INCLUDE_DIR
        "${PROTOCORE_DIR}/headers" ABSOLUTE)
    set(PROTOPYTHON_PROTOCORE_EXTERNAL FALSE)
    # Build-tree RPATH so executables and libraries find each other without
    # LD_LIBRARY_PATH / DYLD_LIBRARY_PATH.
    set(CMAKE_BUILD_RPATH "${CMAKE_BINARY_DIR}/src/library" "${CMAKE_BINARY_DIR}/protoCore")
    set(CMAKE_BUILD_RPATH_USE_LINK_PATH TRUE)
endif()
```

- [ ] **Step 3: Fix the protopyc path condition**

`protoPython/src/compiler/CMakeLists.txt:16` reads `if(DEFINED PROTO_CORE_PREFIX)` and, when true, appends protoCore's own include and library directories to the paths baked into `protopyc` (`:18-19`). After Step 2 an installed protoCore can be found in a default prefix with `PROTO_CORE_PREFIX` unset, so that condition would silently stop adding them. Replace `:16`:

```cmake
if(PROTOPYTHON_PROTOCORE_EXTERNAL)
    # protoCore is installed separately: add its own directories.
    string(APPEND _protopyc_install_include_dirs ":${PROTOPYTHON_PROTOCORE_INCLUDE_DIR}")
    string(APPEND _protopyc_install_library_dirs ":$<TARGET_FILE_DIR:protoCore>")
endif()
```

`$<TARGET_FILE_DIR:protoCore>` resolves through the alias to the imported library's directory, so the generated `protopyc_paths.h` keeps naming a real directory.

- [ ] **Step 4: Guard the CPack generators and version the dependency**

Replace `protoPython/CMakeLists.txt:1056-1074` (`if(UNIX)` through the `endif()` before `include(CPack)`) with:

```cmake
# Platform-specific configurations.
if(APPLE)
    # macOS drag-and-drop .dmg. UNVERIFIED: no macOS build host (D-I6).
    set(CPACK_GENERATOR "DragNDrop")
elseif(UNIX)
    # Enable only the generators whose tools are present: cpack aborts the whole
    # run when one of them is missing, which would take the TGZ down with it.
    # Same pattern as protoCore/CMakeLists.txt:235-247.
    set(CPACK_GENERATOR "TGZ")
    find_program(PROTOPYTHON_DPKG_EXECUTABLE dpkg)
    find_program(PROTOPYTHON_RPMBUILD_EXECUTABLE rpmbuild)
    if(PROTOPYTHON_DPKG_EXECUTABLE)
        list(APPEND CPACK_GENERATOR "DEB")
        message(STATUS "CPack: DEB generator enabled (dpkg found)")
    else()
        message(STATUS "CPack: DEB generator disabled (dpkg not found)")
    endif()
    if(PROTOPYTHON_RPMBUILD_EXECUTABLE)
        list(APPEND CPACK_GENERATOR "RPM")
        message(STATUS "CPack: RPM generator enabled (rpmbuild found)")
    else()
        message(STATUS "CPack: RPM generator disabled (rpmbuild not found)")
    endif()
    set(CPACK_DEBIAN_PACKAGE_NAME "protopython")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "gamarino@gmail.com")
    set(CPACK_DEBIAN_PACKAGE_SECTION "interpreters")
    # protopy and libprotoPython link libprotoCore. protoCore's major version
    # and its soname move together, so the dependency is bounded on both sides:
    # a 3.x protoCore would be ABI-incompatible with this build.
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "protocore (>= ${PROTOCORE_MIN_VERSION_FULL}), protocore (<< ${PROTOCORE_NEXT_MAJOR})")
    set(CPACK_RPM_PACKAGE_NAME "protoPython")
    set(CPACK_RPM_PACKAGE_LICENSE "MIT")
    set(CPACK_RPM_PACKAGE_GROUP "Development/Languages")
    set(CPACK_RPM_PACKAGE_REQUIRES
        "protoCore >= ${PROTOCORE_MIN_VERSION_FULL}, protoCore < ${PROTOCORE_NEXT_MAJOR}")
elseif(WIN32)
    # UNVERIFIED: no Windows build host (D-I6).
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_HELP_LINK "https://github.com/gamarino/protoPython")
    set(CPACK_NSIS_PACKAGE_NAME "protoPython")
endif()
```

Note the reordering: `if(APPLE)` comes first, because the original nested `if(UNIX)/if(APPLE)` is equivalent and the flat form matches the other four repositories.

- [ ] **Step 5: Build both ways and check the generator list**

```bash
W=/home/gamarino/Documentos/proyectos
S=$W/.agent_scratch/phase-i-installers
# Developer mode: no package, sibling fallback, expect the WARNING.
rm -rf $W/protoPython/build_devcheck
cmake -S $W/protoPython -B $W/protoPython/build_devcheck -DCMAKE_BUILD_TYPE=Release 2>&1 | tee $S/protopython-dev-configure.txt | tail -20
# Packaging mode: installed package required.
rm -rf $W/protoPython/build_pkg
cmake -S $W/protoPython -B $W/protoPython/build_pkg -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH=$S/stage -DCMAKE_INSTALL_PREFIX=$S/stage \
    -DPROTOCORE_REQUIRE_PACKAGE=ON 2>&1 | tee $S/protopython-pkg-configure.txt | grep -E "protoPython:|CPack:"
cmake --build $W/protoPython/build_pkg -j4
```
Expected in `protopython-dev-configure.txt`: the `message(WARNING)` about the developer mode. Expected in `protopython-pkg-configure.txt`: `protoPython: installed protoCore 2.0.0 (SOVERSION 2) from .../stage/lib/cmake/protoCore`, `CPack: DEB generator enabled (dpkg found)` and `CPack: RPM generator disabled (rpmbuild not found)`.

- [ ] **Step 6: Run the test suite from the packaging build**

```bash
W=/home/gamarino/Documentos/proyectos
S=$W/.agent_scratch/phase-i-installers
ctest --test-dir $W/protoPython/build_pkg --output-on-failure 2>&1 | tail -15 | tee $S/protopython-ctest.txt
```
Expected: the same pass count as the repository's current baseline. Any newly failing test is a Task 2 regression until proved otherwise.

- [ ] **Step 7: Documentation**

In `protoPython/docs/INSTALLATION.md`, state the new protoCore requirement (`find_package(protoCore 2.0 REQUIRED CONFIG)`, `-DCMAKE_PREFIX_PATH=<prefix>`, `-DPROTOCORE_REQUIRE_PACKAGE=ON` for packaging builds, and the fact that a prefix holding only a library and a header is no longer accepted), and append the same "Platform verification status" table shape as Task 1 Step 11, with:

| Platform | Packaging | Status |
|----------|-----------|--------|
| Linux | TGZ, DEB (needs `dpkg`), RPM (needs `rpmbuild`) | Built, installed to a scratch prefix and smoke-tested, including the installed `lib/protoPython/python3.14` standard library |
| macOS | DragNDrop | Configured and reviewed, **never built** — no macOS host |
| Windows | NSIS, ZIP | Configured and reviewed, **never built** — no Windows host |

- [ ] **Step 8: Commit**

```bash
W=/home/gamarino/Documentos/proyectos
git -C $W/protoPython add CMakeLists.txt src/compiler/CMakeLists.txt docs/INSTALLATION.md
git -C $W/protoPython commit -m "$(cat <<'EOF'
Find protoCore through its CMake package and guard the CPack generators

Prefer find_package(protoCore 2.0 CONFIG) over the version-blind find_library
search, keeping the sibling ../protoCore build as an explicit developer
fallback that warns and can be forbidden with -DPROTOCORE_REQUIRE_PACKAGE=ON.
Enable the DEB and RPM generators only when dpkg and rpmbuild are present, and
bound the protoCore dependency to >= 2.0.0 and < 3.0.0.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

**Done-when:**

```bash
W=/home/gamarino/Documentos/proyectos
grep -q "CPack: RPM generator disabled" $W/.agent_scratch/phase-i-installers/protopython-pkg-configure.txt \
  && grep -q "installed protoCore 2.0.0 (SOVERSION 2)" $W/.agent_scratch/phase-i-installers/protopython-pkg-configure.txt \
  && (cd $W/protoPython/build_pkg && cpack -G DEB >/dev/null 2>&1) \
  && dpkg -I $W/protoPython/build_pkg/protopython-*.deb | grep -q "Depends: protocore (>= 2.0.0), protocore (<< 3.0.0)" \
  && echo "TASK 2 DONE"
```
prints `TASK 2 DONE`.

---

### Task 3: protoST — `find_package`, guarded generators, versioned dependency

**Files:**
- Modify: `protoST/CMakeLists.txt` (`:21-52` and `:222-242`)
- Create: `protoST/docs/INSTALLATION.md`

**Interfaces:**
- Consumes: `protoCore::protoCore`, `protoCore_VERSION`, `protoCore_SOVERSION`, `protoCore_INCLUDE_DIR` from Task 1.
- Produces: `PROTOCORE_LIBRARY` (`protoCore::protoCore` in package mode) consumed by `CMakeLists.txt:113` and `tests/CMakeLists.txt:12`; `PROTOCORE_INCLUDE_DIRS` consumed by `CMakeLists.txt:54`, `:70` and `:99`; a `protost` DEB depending on `protocore (>= 2.0.0), protocore (<< 3.0.0)`.

- [ ] **Step 1: Branch**

```bash
W=/home/gamarino/Documentos/proyectos
git -C $W/protoST status --short
git -C $W/protoST switch -c feature/phase-i-installers
```

- [ ] **Step 2: Replace the protoCore discovery block**

Replace `protoST/CMakeLists.txt:21-52` (the comment at `:21` through the `endif()` at `:52`) with:

```cmake
# --- protoCore -----------------------------------------------------------------
# Resolution order (Phase I, decision D-I1):
#   1. An installed protoCore CMake package: find_package(protoCore 2.0 CONFIG).
#      This is the only mode that checks protoCore's version and ABI, so it is
#      the only mode a distributable package may be built in. Name a prefix with
#      -DPROTO_CORE_PREFIX=<prefix> or -DCMAKE_PREFIX_PATH=<prefix>.
#   2. Developer fallback: the sibling source tree ../protoCore with one of its
#      build directories already populated. Search order build_release, build,
#      build_check - the first directory holding libprotoCore wins, and
#      build_release comes first so a leftover build/ cannot shadow it. Used
#      only when no installed package was found and no prefix was named;
#      refused by -DPROTOCORE_REQUIRE_PACKAGE=ON.
set(PROTOCORE_MIN_VERSION      "2.0")
set(PROTOCORE_MIN_VERSION_FULL "2.0.0")
set(PROTOCORE_NEXT_MAJOR       "3.0.0")
set(PROTOCORE_ABI_SOVERSION    "2")
option(PROTOCORE_REQUIRE_PACKAGE
    "Require an installed protoCore CMake package; never fall back to ../protoCore" OFF)

if(DEFINED PROTO_CORE_PREFIX)
    list(APPEND CMAKE_PREFIX_PATH "${PROTO_CORE_PREFIX}")
endif()

find_package(protoCore ${PROTOCORE_MIN_VERSION} QUIET CONFIG)

if(protoCore_FOUND)
    if(NOT protoCore_SOVERSION STREQUAL PROTOCORE_ABI_SOVERSION)
        message(FATAL_ERROR
            "protoCore package at ${protoCore_DIR} declares SOVERSION "
            "${protoCore_SOVERSION}; protoST is built against SOVERSION "
            "${PROTOCORE_ABI_SOVERSION}. Install a matching protoCore.")
    endif()
    set(PROTOCORE_LIBRARY protoCore::protoCore)
    set(PROTOCORE_INCLUDE_DIRS "${protoCore_INCLUDE_DIR}")
    message(STATUS "protoST: installed protoCore ${protoCore_VERSION} "
                   "(SOVERSION ${protoCore_SOVERSION}) from ${protoCore_DIR}")
else()
    set(_protoCore_where "")
    if(DEFINED PROTO_CORE_PREFIX)
        set(_protoCore_where " under ${PROTO_CORE_PREFIX}")
    endif()
    if(DEFINED PROTO_CORE_PREFIX OR PROTOCORE_REQUIRE_PACKAGE)
        message(FATAL_ERROR
            "No protoCore >= ${PROTOCORE_MIN_VERSION_FULL} CMake package was "
            "found${_protoCore_where}.\n"
            "  A prefix holding only libprotoCore and protoCore.h is no longer "
            "accepted: without lib/cmake/protoCore/protoCoreConfig.cmake there "
            "is no way to tell protoCore 1.x from 2.x. Install protoCore "
            "${PROTOCORE_MIN_VERSION_FULL} or later:\n"
            "    cmake -S ../protoCore -B ../protoCore/build_release "
            "-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=<prefix>\n"
            "    cmake --build ../protoCore/build_release --target protoCore\n"
            "    cmake --install ../protoCore/build_release --component protoCore")
    endif()
    unset(_protoCore_where)
    message(WARNING
        "protoST: no installed protoCore package found; using the developer "
        "build in ../protoCore. This mode performs no package version check "
        "and must not be used to produce a distributable package (configure "
        "with -DPROTOCORE_REQUIRE_PACKAGE=ON to forbid it).")
    set(PROTOCORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../protoCore)
    # Pass -DPROTOCORE_LIBRARY=<path> to choose explicitly; delete the cache
    # entry to search again.
    find_library(PROTOCORE_LIBRARY NAMES protoCore
        PATHS ${PROTOCORE_DIR}/build_release ${PROTOCORE_DIR}/build ${PROTOCORE_DIR}/build_check
        NO_DEFAULT_PATH)
    if(NOT PROTOCORE_LIBRARY)
        message(FATAL_ERROR
            "protoCore shared library not found. Build it first:\n"
            "  cd ${PROTOCORE_DIR} && cmake -B build_release -S . && cmake --build build_release --target protoCore")
    endif()
    get_filename_component(PROTOCORE_LIB_DIR "${PROTOCORE_LIBRARY}" DIRECTORY)
    if(APPLE)
        set(_protoCore_abi_file "${PROTOCORE_LIB_DIR}/libprotoCore.${PROTOCORE_ABI_SOVERSION}.dylib")
    else()
        set(_protoCore_abi_file "${PROTOCORE_LIB_DIR}/libprotoCore.so.${PROTOCORE_ABI_SOVERSION}")
    endif()
    if(NOT EXISTS "${_protoCore_abi_file}")
        message(FATAL_ERROR
            "The protoCore build in ${PROTOCORE_LIB_DIR} does not provide "
            "${_protoCore_abi_file}, so it is not SOVERSION "
            "${PROTOCORE_ABI_SOVERSION}. Rebuild ../protoCore from clean.")
    endif()
    unset(_protoCore_abi_file)
    set(PROTOCORE_INCLUDE_DIRS ${PROTOCORE_DIR} ${PROTOCORE_DIR}/headers)
    set(CMAKE_BUILD_RPATH "${PROTOCORE_LIB_DIR}")
    set(CMAKE_BUILD_RPATH_USE_LINK_PATH TRUE)
endif()
```

`include_directories(${PROTOCORE_INCLUDE_DIRS} include)` at `:54` and the two `target_include_directories` calls at `:70` and `:99` are left exactly as they are: in package mode the variable now holds the installed include directory, which is what they need.

- [ ] **Step 3: Guard the CPack generators and version the dependency**

Replace `protoST/CMakeLists.txt:222-242` (the `# Platform-specific generators.` comment through the `endif()`) with:

```cmake
# Platform-specific generators.
if(APPLE)
    # macOS drag-and-drop .dmg. UNVERIFIED: no macOS build host (D-I6).
    set(CPACK_GENERATOR "DragNDrop")
elseif(UNIX)
    # Enable only the generators whose tools are present: cpack aborts the whole
    # run when one of them is missing. Same pattern as
    # protoCore/CMakeLists.txt:235-247.
    set(CPACK_GENERATOR "TGZ")
    find_program(PROTOST_DPKG_EXECUTABLE dpkg)
    find_program(PROTOST_RPMBUILD_EXECUTABLE rpmbuild)
    if(PROTOST_DPKG_EXECUTABLE)
        list(APPEND CPACK_GENERATOR "DEB")
        message(STATUS "CPack: DEB generator enabled (dpkg found)")
    else()
        message(STATUS "CPack: DEB generator disabled (dpkg not found)")
    endif()
    if(PROTOST_RPMBUILD_EXECUTABLE)
        list(APPEND CPACK_GENERATOR "RPM")
        message(STATUS "CPack: RPM generator enabled (rpmbuild found)")
    else()
        message(STATUS "CPack: RPM generator disabled (rpmbuild not found)")
    endif()
    # protoST links libprotoCore. protoCore's package is named "protocore" for
    # DEB and "protoCore" for RPM (pinned in protoCore/CMakeLists.txt). Its
    # major version and its soname move together, so the dependency is bounded
    # on both sides.
    set(CPACK_DEBIAN_PACKAGE_NAME "protost")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "gamarino@gmail.com")
    set(CPACK_DEBIAN_PACKAGE_SECTION "interpreters")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "protocore (>= ${PROTOCORE_MIN_VERSION_FULL}), protocore (<< ${PROTOCORE_NEXT_MAJOR})")
    set(CPACK_RPM_PACKAGE_NAME "protoST")
    set(CPACK_RPM_PACKAGE_LICENSE "MIT")
    set(CPACK_RPM_PACKAGE_GROUP "Development/Languages")
    set(CPACK_RPM_PACKAGE_REQUIRES
        "protoCore >= ${PROTOCORE_MIN_VERSION_FULL}, protoCore < ${PROTOCORE_NEXT_MAJOR}")
elseif(WIN32)
    # Windows NSIS installer + plain ZIP. UNVERIFIED: no Windows host (D-I6).
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_HELP_LINK "https://github.com/gamarino/protoST")
    set(CPACK_NSIS_PACKAGE_NAME "protoST")
endif()
```

- [ ] **Step 4: Configure and build both ways**

```bash
W=/home/gamarino/Documentos/proyectos
S=$W/.agent_scratch/phase-i-installers
rm -rf $W/protoST/build_pkg
cmake -S $W/protoST -B $W/protoST/build_pkg -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH=$S/stage -DCMAKE_INSTALL_PREFIX=$S/stage \
    -DPROTOCORE_REQUIRE_PACKAGE=ON 2>&1 | tee $S/protost-pkg-configure.txt | grep -E "protoST:|CPack:"
cmake --build $W/protoST/build_pkg -j4
ctest --test-dir $W/protoST/build_pkg --output-on-failure 2>&1 | tail -15 | tee $S/protost-ctest.txt
```
Expected: `protoST: installed protoCore 2.0.0 (SOVERSION 2) from ...`, `CPack: DEB generator enabled (dpkg found)`, `CPack: RPM generator disabled (rpmbuild not found)`, and a ctest summary matching the repository's baseline.

- [ ] **Step 5: Create `protoST/docs/INSTALLATION.md`**

The repository has no installation document (`ls protoST/docs` shows `STATUS.md`, `LANGUAGE.md`, `ROADMAP.md`, `INTEROP.md`, `debugging.md`, `TUTORIAL.md` and the tutorial tree). Create one covering: prerequisites (C++20, CMake ≥ 3.20, `libreadline`, protoCore ≥ 2.0.0 installed); building against an installed protoCore and against the sibling tree; the install layout (`bin/protost`, `share/protoST/lib/*.st`, `share/doc/...`, and the VS Code integration when present); how `protost` finds its standard library (`$PROTOST_LIB`, then paths derived from the executable, then `<cwd>/lib`) with the note that after Task 7 the executable-derived step works on Linux, macOS and Windows; building packages with `cpack`; and this table:

| Platform | Packaging | Status |
|----------|-----------|--------|
| Linux | TGZ, DEB (needs `dpkg`), RPM (needs `rpmbuild`) | Built, installed to a scratch prefix and smoke-tested, including `Import from: 'stream'` resolving out of `share/protoST/lib` |
| macOS | DragNDrop | Configured and reviewed, **never built** — no macOS host |
| Windows | NSIS, ZIP | Configured and reviewed, **never built** — no Windows host |

- [ ] **Step 6: Commit** (protoST's git identity is `gamarino@gmail.com`; do not override it)

```bash
W=/home/gamarino/Documentos/proyectos
git -C $W/protoST add CMakeLists.txt docs/INSTALLATION.md
git -C $W/protoST commit -m "$(cat <<'EOF'
Find protoCore through its CMake package and guard the CPack generators

Prefer find_package(protoCore 2.0 CONFIG) over the version-blind find_library
search, keeping the sibling ../protoCore build directories as an explicit
developer fallback that warns, asserts SOVERSION 2 and can be forbidden with
-DPROTOCORE_REQUIRE_PACKAGE=ON. Enable the DEB and RPM generators only when
dpkg and rpmbuild are present, bound the protoCore dependency to >= 2.0.0 and
< 3.0.0, and add docs/INSTALLATION.md.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

**Done-when:**

```bash
W=/home/gamarino/Documentos/proyectos
grep -q "CPack: RPM generator disabled" $W/.agent_scratch/phase-i-installers/protost-pkg-configure.txt \
  && (cd $W/protoST/build_pkg && cpack -G DEB >/dev/null 2>&1) \
  && dpkg -I $W/protoST/build_pkg/protost-*.deb | grep -q "Depends: protocore (>= 2.0.0), protocore (<< 3.0.0)" \
  && dpkg -c $W/protoST/build_pkg/protost-*.deb | grep -q "share/protoST/lib/stream.st" \
  && echo "TASK 3 DONE"
```
prints `TASK 3 DONE`.

---

### Task 4: protoClojure — `find_package`, guarded generators, versioned dependency

**Files:**
- Modify: `protoClojure/CMakeLists.txt` (`:19-47` and `:192-213`)
- Create: `protoClojure/docs/INSTALLATION.md`

**Interfaces:**
- Consumes: `protoCore::protoCore`, `protoCore_VERSION`, `protoCore_SOVERSION`, `protoCore_INCLUDE_DIR` from Task 1.
- Produces: `PROTOCORE_LIBRARY` consumed at `CMakeLists.txt:58`, `:66`, `:78` and `:125`; `PROTOCORE_INCLUDE_DIRS` consumed at `:49` and `:126`; a `protoclojure` DEB depending on `protocore (>= 2.0.0), protocore (<< 3.0.0)`.

- [ ] **Step 1: Branch**

```bash
W=/home/gamarino/Documentos/proyectos
git -C $W/protoClojure status --short
git -C $W/protoClojure switch -c feature/phase-i-installers
```

- [ ] **Step 2: Replace the protoCore discovery block**

Replace `protoClojure/CMakeLists.txt:19-47` (the comment at `:19` through the `endif()` at `:47`) with the block below. It is the same shape as Task 3's but names protoClojure in its messages, so the error a user sees says which project refused to configure:

```cmake
# --- protoCore -----------------------------------------------------------------
# Resolution order (Phase I, decision D-I1):
#   1. An installed protoCore CMake package: find_package(protoCore 2.0 CONFIG).
#      The only mode that checks protoCore's version and ABI, and therefore the
#      only mode a distributable package may be built in. Name a prefix with
#      -DPROTO_CORE_PREFIX=<prefix> or -DCMAKE_PREFIX_PATH=<prefix>.
#   2. Developer fallback: the sibling source tree ../protoCore with one of its
#      build_* directories already populated. Used only when no installed
#      package was found and no prefix was named; refused by
#      -DPROTOCORE_REQUIRE_PACKAGE=ON.
set(PROTOCORE_MIN_VERSION      "2.0")
set(PROTOCORE_MIN_VERSION_FULL "2.0.0")
set(PROTOCORE_NEXT_MAJOR       "3.0.0")
set(PROTOCORE_ABI_SOVERSION    "2")
option(PROTOCORE_REQUIRE_PACKAGE
    "Require an installed protoCore CMake package; never fall back to ../protoCore" OFF)

if(DEFINED PROTO_CORE_PREFIX)
    list(APPEND CMAKE_PREFIX_PATH "${PROTO_CORE_PREFIX}")
endif()

find_package(protoCore ${PROTOCORE_MIN_VERSION} QUIET CONFIG)

if(protoCore_FOUND)
    if(NOT protoCore_SOVERSION STREQUAL PROTOCORE_ABI_SOVERSION)
        message(FATAL_ERROR
            "protoCore package at ${protoCore_DIR} declares SOVERSION "
            "${protoCore_SOVERSION}; protoClojure is built against SOVERSION "
            "${PROTOCORE_ABI_SOVERSION}. Install a matching protoCore.")
    endif()
    set(PROTOCORE_LIBRARY protoCore::protoCore)
    set(PROTOCORE_INCLUDE_DIRS "${protoCore_INCLUDE_DIR}")
    message(STATUS "protoClojure: installed protoCore ${protoCore_VERSION} "
                   "(SOVERSION ${protoCore_SOVERSION}) from ${protoCore_DIR}")
else()
    set(_protoCore_where "")
    if(DEFINED PROTO_CORE_PREFIX)
        set(_protoCore_where " under ${PROTO_CORE_PREFIX}")
    endif()
    if(DEFINED PROTO_CORE_PREFIX OR PROTOCORE_REQUIRE_PACKAGE)
        message(FATAL_ERROR
            "No protoCore >= ${PROTOCORE_MIN_VERSION_FULL} CMake package was "
            "found${_protoCore_where}.\n"
            "  A prefix holding only libprotoCore and protoCore.h is no longer "
            "accepted: without lib/cmake/protoCore/protoCoreConfig.cmake there "
            "is no way to tell protoCore 1.x from 2.x. Install protoCore "
            "${PROTOCORE_MIN_VERSION_FULL} or later:\n"
            "    cmake -S ../protoCore -B ../protoCore/build_release "
            "-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=<prefix>\n"
            "    cmake --build ../protoCore/build_release --target protoCore\n"
            "    cmake --install ../protoCore/build_release --component protoCore")
    endif()
    unset(_protoCore_where)
    message(WARNING
        "protoClojure: no installed protoCore package found; using the "
        "developer build in ../protoCore. This mode performs no package "
        "version check and must not be used to produce a distributable "
        "package (configure with -DPROTOCORE_REQUIRE_PACKAGE=ON to forbid it).")
    set(PROTOCORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../protoCore)
    find_library(PROTOCORE_LIBRARY NAMES protoCore
        PATHS ${PROTOCORE_DIR}/build_release ${PROTOCORE_DIR}/build ${PROTOCORE_DIR}/build_check
        NO_DEFAULT_PATH)
    if(NOT PROTOCORE_LIBRARY)
        message(FATAL_ERROR
            "protoCore shared library not found. Build it first:\n"
            "  cd ${PROTOCORE_DIR} && cmake -B build_release -S . && cmake --build build_release --target protoCore")
    endif()
    get_filename_component(PROTOCORE_LIB_DIR "${PROTOCORE_LIBRARY}" DIRECTORY)
    if(APPLE)
        set(_protoCore_abi_file "${PROTOCORE_LIB_DIR}/libprotoCore.${PROTOCORE_ABI_SOVERSION}.dylib")
    else()
        set(_protoCore_abi_file "${PROTOCORE_LIB_DIR}/libprotoCore.so.${PROTOCORE_ABI_SOVERSION}")
    endif()
    if(NOT EXISTS "${_protoCore_abi_file}")
        message(FATAL_ERROR
            "The protoCore build in ${PROTOCORE_LIB_DIR} does not provide "
            "${_protoCore_abi_file}, so it is not SOVERSION "
            "${PROTOCORE_ABI_SOVERSION}. Rebuild ../protoCore from clean.")
    endif()
    unset(_protoCore_abi_file)
    set(PROTOCORE_INCLUDE_DIRS ${PROTOCORE_DIR} ${PROTOCORE_DIR}/headers)
    set(CMAKE_BUILD_RPATH "${PROTOCORE_LIB_DIR}")
    set(CMAKE_BUILD_RPATH_USE_LINK_PATH TRUE)
endif()
```

- [ ] **Step 3: Guard the CPack generators and version the dependency**

Replace `protoClojure/CMakeLists.txt:192-213` (the `# Platform-specific generators.` comment block through the `endif()`) with:

```cmake
# Platform-specific generators. protoClojure links libprotoCore, so every
# generator's package metadata declares a bounded dependency on protoCore's
# package: "protocore" for DEB, "protoCore" for RPM (both pinned in
# protoCore/CMakeLists.txt), and the NSIS help link for Windows.
if(APPLE)
    # macOS drag-and-drop .dmg. UNVERIFIED: no macOS build host (D-I6).
    set(CPACK_GENERATOR "DragNDrop")
elseif(UNIX)
    # Enable only the generators whose tools are present: cpack aborts the whole
    # run when one of them is missing. Same pattern as
    # protoCore/CMakeLists.txt:235-247.
    set(CPACK_GENERATOR "TGZ")
    find_program(PROTOCLOJURE_DPKG_EXECUTABLE dpkg)
    find_program(PROTOCLOJURE_RPMBUILD_EXECUTABLE rpmbuild)
    if(PROTOCLOJURE_DPKG_EXECUTABLE)
        list(APPEND CPACK_GENERATOR "DEB")
        message(STATUS "CPack: DEB generator enabled (dpkg found)")
    else()
        message(STATUS "CPack: DEB generator disabled (dpkg not found)")
    endif()
    if(PROTOCLOJURE_RPMBUILD_EXECUTABLE)
        list(APPEND CPACK_GENERATOR "RPM")
        message(STATUS "CPack: RPM generator enabled (rpmbuild found)")
    else()
        message(STATUS "CPack: RPM generator disabled (rpmbuild not found)")
    endif()
    set(CPACK_DEBIAN_PACKAGE_NAME "protoclojure")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "gamarino@gmail.com")
    set(CPACK_DEBIAN_PACKAGE_SECTION "interpreters")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "protocore (>= ${PROTOCORE_MIN_VERSION_FULL}), protocore (<< ${PROTOCORE_NEXT_MAJOR})")
    set(CPACK_RPM_PACKAGE_NAME "protoClojure")
    set(CPACK_RPM_PACKAGE_LICENSE "MIT")
    set(CPACK_RPM_PACKAGE_GROUP "Development/Languages")
    set(CPACK_RPM_PACKAGE_REQUIRES
        "protoCore >= ${PROTOCORE_MIN_VERSION_FULL}, protoCore < ${PROTOCORE_NEXT_MAJOR}")
elseif(WIN32)
    # Windows NSIS installer + plain ZIP. UNVERIFIED: no Windows host (D-I6).
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_HELP_LINK      "https://github.com/gamarino/protoClojure")
    set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/gamarino/protoClojure")
    set(CPACK_NSIS_PACKAGE_NAME   "protoClojure")
endif()
```

- [ ] **Step 4: Configure, build and test**

```bash
W=/home/gamarino/Documentos/proyectos
S=$W/.agent_scratch/phase-i-installers
rm -rf $W/protoClojure/build_pkg
cmake -S $W/protoClojure -B $W/protoClojure/build_pkg -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH=$S/stage -DCMAKE_INSTALL_PREFIX=$S/stage \
    -DPROTOCORE_REQUIRE_PACKAGE=ON 2>&1 | tee $S/protoclojure-pkg-configure.txt | grep -E "protoClojure:|CPack:"
cmake --build $W/protoClojure/build_pkg -j4
ctest --test-dir $W/protoClojure/build_pkg -j1 --output-on-failure 2>&1 | tail -15 | tee $S/protoclojure-ctest.txt
```

- [ ] **Step 5: Create `protoClojure/docs/INSTALLATION.md`**

Cover: prerequisites (C++20, CMake ≥ 3.20, `libreadline` — a hard `FATAL_ERROR` at `CMakeLists.txt:92-98` — and protoCore ≥ 2.0.0 installed); building against an installed protoCore and against the sibling tree; the install layout (`bin/protoclj`, `share/doc/...`, `share/protoClojure/examples/*.clj`, `share/protoClojure/benchmarks/`); the explicit statement that `protoclj` needs **no** external data files at runtime (the examples and benchmarks are reference material, not a runtime dependency); building packages with `cpack`; and the platform table:

| Platform | Packaging | Status |
|----------|-----------|--------|
| Linux | TGZ, DEB (needs `dpkg`), RPM (needs `rpmbuild`) | Built, installed to a scratch prefix and smoke-tested |
| macOS | DragNDrop | Configured and reviewed, **never built** — no macOS host |
| Windows | NSIS, ZIP | Configured and reviewed, **never built** — no Windows host |

- [ ] **Step 6: Commit**

```bash
W=/home/gamarino/Documentos/proyectos
git -C $W/protoClojure add CMakeLists.txt docs/INSTALLATION.md
git -C $W/protoClojure commit -m "$(cat <<'EOF'
Find protoCore through its CMake package and guard the CPack generators

Prefer find_package(protoCore 2.0 CONFIG) over the version-blind find_library
search, keeping the sibling ../protoCore build directories as an explicit
developer fallback that warns, asserts SOVERSION 2 and can be forbidden with
-DPROTOCORE_REQUIRE_PACKAGE=ON. Enable the DEB and RPM generators only when
dpkg and rpmbuild are present, bound the protoCore dependency to >= 2.0.0 and
< 3.0.0, and add docs/INSTALLATION.md.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

**Done-when:**

```bash
W=/home/gamarino/Documentos/proyectos
grep -q "CPack: RPM generator disabled" $W/.agent_scratch/phase-i-installers/protoclojure-pkg-configure.txt \
  && (cd $W/protoClojure/build_pkg && cpack -G DEB >/dev/null 2>&1) \
  && dpkg -I $W/protoClojure/build_pkg/protoclojure-*.deb | grep -q "Depends: protocore (>= 2.0.0), protocore (<< 3.0.0)" \
  && echo "TASK 4 DONE"
```
prints `TASK 4 DONE`.

---

### Task 5: protoScala — `find_package`, and CPack from scratch (D-I3)

**Files:**
- Modify: `protoScala/CMakeLists.txt` (`:19-47`, `:132-139`, and a new block appended after `:193`)
- Create: `protoScala/docs/INSTALLATION.md`
- Modify: `protoScala/docs/DECISIONS-LOG.md`

**Interfaces:**
- Consumes: `protoCore::protoCore`, `protoCore_VERSION`, `protoCore_SOVERSION`, `protoCore_INCLUDE_DIR` from Task 1.
- Produces: `PROTOCORE_LIBRARY` consumed at `CMakeLists.txt:85`, `:126` and `:171`; `PROTOCORE_INCLUDE_DIRS` consumed at `:49`, `:133` and `:172`; a `protoscala` DEB depending on `protocore (>= 2.0.0), protocore (<< 3.0.0)` — the first package this repository has ever produced.

- [ ] **Step 1: Re-read the file, then branch**

A concurrent agent is committing to protoScala `main`. Re-read `protoScala/CMakeLists.txt` and confirm the three anchors before editing: `:23` is `if(DEFINED PROTO_CORE_PREFIX)`, `:132` is `set(PROTOSCALA_PMQ_FOUND -1)`, and `:193` is the last line (`COMPONENT protoScala`). If any of them has moved, re-locate them by content; if the protoCore block has been restructured, **stop and ask**.

```bash
W=/home/gamarino/Documentos/proyectos
git -C $W/protoScala status --short
git -C $W/protoScala pull --ff-only 2>/dev/null || true
git -C $W/protoScala switch -c feature/phase-i-installers
```

- [ ] **Step 2: Replace the protoCore discovery block**

Replace `protoScala/CMakeLists.txt:19-47` with:

```cmake
# --- protoCore ----------------------------------------------------------------
# Resolution order (Phase I, decision D-I1):
#   1. An installed protoCore CMake package: find_package(protoCore 2.0 CONFIG).
#      The only mode that checks protoCore's version and ABI, and therefore the
#      only mode a distributable package may be built in. Name a prefix with
#      -DPROTO_CORE_PREFIX=<prefix> or -DCMAKE_PREFIX_PATH=<prefix>.
#   2. Developer fallback: the sibling source tree ../protoCore with one of its
#      build directories already populated (same logic as protoST and
#      protoClojure). Used only when no installed package was found and no
#      prefix was named; refused by -DPROTOCORE_REQUIRE_PACKAGE=ON.
set(PROTOCORE_MIN_VERSION      "2.0")
set(PROTOCORE_MIN_VERSION_FULL "2.0.0")
set(PROTOCORE_NEXT_MAJOR       "3.0.0")
set(PROTOCORE_ABI_SOVERSION    "2")
option(PROTOCORE_REQUIRE_PACKAGE
    "Require an installed protoCore CMake package; never fall back to ../protoCore" OFF)

if(DEFINED PROTO_CORE_PREFIX)
    list(APPEND CMAKE_PREFIX_PATH "${PROTO_CORE_PREFIX}")
endif()

find_package(protoCore ${PROTOCORE_MIN_VERSION} QUIET CONFIG)

if(protoCore_FOUND)
    if(NOT protoCore_SOVERSION STREQUAL PROTOCORE_ABI_SOVERSION)
        message(FATAL_ERROR
            "protoCore package at ${protoCore_DIR} declares SOVERSION "
            "${protoCore_SOVERSION}; protoScala is built against SOVERSION "
            "${PROTOCORE_ABI_SOVERSION}. Install a matching protoCore.")
    endif()
    set(PROTOCORE_LIBRARY protoCore::protoCore)
    set(PROTOCORE_INCLUDE_DIRS "${protoCore_INCLUDE_DIR}")
    message(STATUS "protoScala: installed protoCore ${protoCore_VERSION} "
                   "(SOVERSION ${protoCore_SOVERSION}) from ${protoCore_DIR}")
else()
    set(_protoCore_where "")
    if(DEFINED PROTO_CORE_PREFIX)
        set(_protoCore_where " under ${PROTO_CORE_PREFIX}")
    endif()
    if(DEFINED PROTO_CORE_PREFIX OR PROTOCORE_REQUIRE_PACKAGE)
        message(FATAL_ERROR
            "No protoCore >= ${PROTOCORE_MIN_VERSION_FULL} CMake package was "
            "found${_protoCore_where}.\n"
            "  A prefix holding only libprotoCore and protoCore.h is no longer "
            "accepted: without lib/cmake/protoCore/protoCoreConfig.cmake there "
            "is no way to tell protoCore 1.x from 2.x, and protoScala's "
            "ProtoMPSCQueue feature probe would silently read the wrong "
            "header. Install protoCore ${PROTOCORE_MIN_VERSION_FULL} or later:\n"
            "    cmake -S ../protoCore -B ../protoCore/build_release "
            "-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=<prefix>\n"
            "    cmake --build ../protoCore/build_release --target protoCore\n"
            "    cmake --install ../protoCore/build_release --component protoCore")
    endif()
    unset(_protoCore_where)
    message(WARNING
        "protoScala: no installed protoCore package found; using the developer "
        "build in ../protoCore. This mode performs no package version check "
        "and must not be used to produce a distributable package (configure "
        "with -DPROTOCORE_REQUIRE_PACKAGE=ON to forbid it).")
    set(PROTOCORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../protoCore)
    find_library(PROTOCORE_LIBRARY NAMES protoCore
        PATHS ${PROTOCORE_DIR}/build_release ${PROTOCORE_DIR}/build ${PROTOCORE_DIR}/build_check
        NO_DEFAULT_PATH)
    if(NOT PROTOCORE_LIBRARY)
        message(FATAL_ERROR
            "protoCore shared library not found. Build it first:\n"
            "  cd ${PROTOCORE_DIR} && cmake -B build_release -S . && cmake --build build_release --target protoCore")
    endif()
    get_filename_component(PROTOCORE_LIB_DIR "${PROTOCORE_LIBRARY}" DIRECTORY)
    if(APPLE)
        set(_protoCore_abi_file "${PROTOCORE_LIB_DIR}/libprotoCore.${PROTOCORE_ABI_SOVERSION}.dylib")
    else()
        set(_protoCore_abi_file "${PROTOCORE_LIB_DIR}/libprotoCore.so.${PROTOCORE_ABI_SOVERSION}")
    endif()
    if(NOT EXISTS "${_protoCore_abi_file}")
        message(FATAL_ERROR
            "The protoCore build in ${PROTOCORE_LIB_DIR} does not provide "
            "${_protoCore_abi_file}, so it is not SOVERSION "
            "${PROTOCORE_ABI_SOVERSION}. Rebuild ../protoCore from clean.")
    endif()
    unset(_protoCore_abi_file)
    set(PROTOCORE_INCLUDE_DIRS ${PROTOCORE_DIR} ${PROTOCORE_DIR}/headers)
    set(CMAKE_BUILD_RPATH "${PROTOCORE_LIB_DIR}")
    set(CMAKE_BUILD_RPATH_USE_LINK_PATH TRUE)
endif()
```

- [ ] **Step 3: Make the `ProtoMPSCQueue` probe say which header it read**

`protoScala/CMakeLists.txt:132-139` walks `${PROTOCORE_INCLUDE_DIRS}` looking for `protoCore.h` and greps its text for `newMPSCQueue`. With Step 2 that variable holds the installed include directory in package mode, so the probe works unchanged — but the probe is the one place where protoScala's *behaviour* changes according to which protoCore it found, so it must say which file it read. Add one line after the `break()` block and extend the two status messages:

```cmake
set(PROTOSCALA_PMQ_FOUND -1)
set(PROTOSCALA_PROTOCORE_HEADER_PATH "")
foreach(dir ${PROTOCORE_INCLUDE_DIRS})
    if(EXISTS "${dir}/protoCore.h")
        set(PROTOSCALA_PROTOCORE_HEADER_PATH "${dir}/protoCore.h")
        file(READ "${PROTOSCALA_PROTOCORE_HEADER_PATH}" PROTOSCALA_PROTOCORE_HEADER)
        string(FIND "${PROTOSCALA_PROTOCORE_HEADER}" "newMPSCQueue" PROTOSCALA_PMQ_FOUND)
        break()
    endif()
endforeach()
if(NOT PROTOSCALA_PROTOCORE_HEADER_PATH)
    message(FATAL_ERROR
        "No protoCore.h found in ${PROTOCORE_INCLUDE_DIRS}; the actor mailbox "
        "backend cannot be selected.")
endif()
if(PROTOSCALA_PMQ_FOUND GREATER -1)
    message(STATUS "protoScala: actor mailboxes on protoCore ProtoMPSCQueue "
                   "(from ${PROTOSCALA_PROTOCORE_HEADER_PATH})")
    target_compile_definitions(protoscala_runtime PUBLIC PROTOSCALA_HAS_PMQ=1)
else()
    message(STATUS "protoScala: ProtoMPSCQueue not in "
                   "${PROTOSCALA_PROTOCORE_HEADER_PATH} -- actor mailboxes "
                   "fall back to a CAS'd ProtoList")
endif()
```

- [ ] **Step 4: Add the CPack block (D-I3)**

`protoScala/CMakeLists.txt` currently ends at `:193`. Replace the comment at `:176-177` — "An installed bin/protoscala finds libprotoCore under `<prefix>/lib` without LD_LIBRARY_PATH. CPack packaging (.deb / .tar.gz) is completed in Phase 6." — with just its first sentence, and append after `:193`:

```cmake

# --- CPack configuration ---------------------------------------------------
# protoScala ships one executable plus LICENSE and README.md. lib/prelude.scala
# is compiled into the binary (see the Prelude section above), so the package
# carries no runtime data files and there is no build-tree / install-tree path
# to get wrong.
set(CPACK_PACKAGE_NAME "protoscala")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "protoScala - a dynamic Scala 3 dialect on the protoCore runtime")
set(CPACK_PACKAGE_VENDOR "numaes")
set(CPACK_PACKAGE_CONTACT "gamarino@gmail.com")
set(CPACK_RESOURCE_FILE_README  "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_COMPONENTS_ALL protoScala)
set(CPACK_PACKAGE_INSTALL_DIRECTORY "protoScala")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/gamarino/protoScala")

# Platform-specific generators.
if(APPLE)
    # macOS drag-and-drop .dmg. UNVERIFIED: no macOS build host (D-I6).
    set(CPACK_GENERATOR "DragNDrop")
elseif(UNIX)
    # Enable only the generators whose tools are present: cpack aborts the whole
    # run when one of them is missing. Same pattern as
    # protoCore/CMakeLists.txt:235-247.
    set(CPACK_GENERATOR "TGZ")
    find_program(PROTOSCALA_DPKG_EXECUTABLE dpkg)
    find_program(PROTOSCALA_RPMBUILD_EXECUTABLE rpmbuild)
    if(PROTOSCALA_DPKG_EXECUTABLE)
        list(APPEND CPACK_GENERATOR "DEB")
        message(STATUS "CPack: DEB generator enabled (dpkg found)")
    else()
        message(STATUS "CPack: DEB generator disabled (dpkg not found)")
    endif()
    if(PROTOSCALA_RPMBUILD_EXECUTABLE)
        list(APPEND CPACK_GENERATOR "RPM")
        message(STATUS "CPack: RPM generator enabled (rpmbuild found)")
    else()
        message(STATUS "CPack: RPM generator disabled (rpmbuild not found)")
    endif()
    # protoscala links libprotoCore. protoCore's package is named "protocore"
    # for DEB and "protoCore" for RPM (pinned in protoCore/CMakeLists.txt). Its
    # major version and its soname move together, so the dependency is bounded
    # on both sides.
    set(CPACK_DEBIAN_PACKAGE_NAME "protoscala")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "gamarino@gmail.com")
    set(CPACK_DEBIAN_PACKAGE_SECTION "interpreters")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "protocore (>= ${PROTOCORE_MIN_VERSION_FULL}), protocore (<< ${PROTOCORE_NEXT_MAJOR})")
    set(CPACK_RPM_PACKAGE_NAME "protoScala")
    set(CPACK_RPM_PACKAGE_LICENSE "MIT")
    set(CPACK_RPM_PACKAGE_GROUP "Development/Languages")
    set(CPACK_RPM_PACKAGE_REQUIRES
        "protoCore >= ${PROTOCORE_MIN_VERSION_FULL}, protoCore < ${PROTOCORE_NEXT_MAJOR}")
elseif(WIN32)
    # Windows NSIS installer + plain ZIP. UNVERIFIED: no Windows host (D-I6).
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_HELP_LINK      "https://github.com/gamarino/protoScala")
    set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/gamarino/protoScala")
    set(CPACK_NSIS_PACKAGE_NAME   "protoScala")
endif()

include(CPack)
```

`libreadline` is a hard requirement (`CMakeLists.txt:52-58`) and `protoscala` links it, so the DEB should declare it too. CPack's `dpkg-shlibdeps` integration finds it automatically; enable it rather than hand-listing a `libreadline8` that differs per distribution — add, inside the `elseif(UNIX)` branch, just before `CPACK_RPM_PACKAGE_NAME`:

```cmake
    # Let dpkg-shlibdeps discover libreadline and libstdc++ instead of naming
    # per-distribution SONAME packages by hand. libprotoCore is not discovered
    # this way (it is not packaged by a distribution), which is why the
    # protocore relation above is stated explicitly.
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
```

- [ ] **Step 5: Configure, build, test, package**

```bash
W=/home/gamarino/Documentos/proyectos
S=$W/.agent_scratch/phase-i-installers
rm -rf $W/protoScala/build_pkg
cmake -S $W/protoScala -B $W/protoScala/build_pkg -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH=$S/stage -DCMAKE_INSTALL_PREFIX=$S/stage \
    -DPROTOCORE_REQUIRE_PACKAGE=ON 2>&1 | tee $S/protoscala-pkg-configure.txt | grep -E "protoScala:|CPack:"
cmake --build $W/protoScala/build_pkg -j4
ctest --test-dir $W/protoScala/build_pkg --output-on-failure 2>&1 | tail -15 | tee $S/protoscala-ctest.txt
(cd $W/protoScala/build_pkg && cpack -G DEB) 2>&1 | tail -5
dpkg -I $W/protoScala/build_pkg/protoscala-*.deb
dpkg -c $W/protoScala/build_pkg/protoscala-*.deb
```
Expected: `protoScala: installed protoCore 2.0.0 (SOVERSION 2) from ...`; `protoScala: actor mailboxes ... (from .../stage/include/protoCore.h)` or the CAS'd-`ProtoList` line naming the same file; a `.deb` whose `Depends:` contains `protocore (>= 2.0.0), protocore (<< 3.0.0)` and whose contents are `./usr/bin/protoscala` plus the two documentation files.

- [ ] **Step 6: Create `protoScala/docs/INSTALLATION.md`**

Cover: prerequisites (C++20, CMake ≥ 3.20, `libreadline`, protoCore ≥ 2.0.0 installed); the two build modes; the install layout (`bin/protoscala`, `share/doc/protoScala/LICENSE`, `README.md`); the explicit statement that the prelude is compiled into the binary so there are no runtime data files; `cpack` usage; and the platform table:

| Platform | Packaging | Status |
|----------|-----------|--------|
| Linux | TGZ, DEB (needs `dpkg`), RPM (needs `rpmbuild`) | Built, installed to a scratch prefix and smoke-tested |
| macOS | DragNDrop | Configured and reviewed, **never built** — no macOS host |
| Windows | NSIS, ZIP | Configured and reviewed, **never built** — no Windows host |

- [ ] **Step 7: Record the decisions**

Append to `protoScala/docs/DECISIONS-LOG.md` an entry for this phase listing D-I1 through D-I7 with their one-line statements and the marker `[agent, pending review]` — since closed as `[agent, approved by the maintainer on 2026-09-23]` — following the file's existing format for agent-taken entries.

- [ ] **Step 8: Commit**

```bash
W=/home/gamarino/Documentos/proyectos
git -C $W/protoScala add CMakeLists.txt docs/INSTALLATION.md docs/DECISIONS-LOG.md
git -C $W/protoScala commit -m "$(cat <<'EOF'
Add CPack packaging and find protoCore through its CMake package

Prefer find_package(protoCore 2.0 CONFIG) over the version-blind find_library
search, keeping the sibling ../protoCore build directories as an explicit
developer fallback that warns, asserts SOVERSION 2 and can be forbidden with
-DPROTOCORE_REQUIRE_PACKAGE=ON. The ProtoMPSCQueue feature probe now reports
which protoCore.h it read. Add the CPack configuration the file deferred to a
later phase, with DEB and RPM generators guarded on dpkg and rpmbuild and a
protoCore dependency bounded to >= 2.0.0 and < 3.0.0, plus
docs/INSTALLATION.md.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

**Done-when:**

```bash
W=/home/gamarino/Documentos/proyectos
dpkg -I $W/protoScala/build_pkg/protoscala-*.deb | grep -q "Depends:.*protocore (>= 2.0.0), protocore (<< 3.0.0)" \
  && dpkg -c $W/protoScala/build_pkg/protoscala-*.deb | grep -q "usr/bin/protoscala" \
  && echo "TASK 5 DONE"
```
prints `TASK 5 DONE`. Before this task the same `cpack -G DEB` produces nothing at all, because `include(CPack)` does not exist in the file.

---

### Task 6: protoJS — `find_package`, guarded generators, and the `packaging/` version floors (D-I4)

**Files:**
- Modify: `protoJS/CMakeLists.txt` (`:14-51` and `:295-312`)
- Modify: `protoJS/packaging/templates/linux/control.template` (`:5`)
- Modify: `protoJS/packaging/templates/linux/preinst.template` (`:4-27`)
- Modify: `protoJS/packaging/templates/linux/protoJS.spec.template` (`:11`, `:19-38`)
- Modify: `protoJS/packaging/templates/macos/preinstall.template` (`:4-19`)
- Modify: `protoJS/packaging/templates/windows/protoJS.wxs.template` (`:11-26`)
- Modify: `protoJS/packaging/build_deb.sh` (`:8-23`)
- Modify: `protoJS/docs/INSTALLATION.md`

**Interfaces:**
- Consumes: `protoCore::protoCore`, `protoCore_VERSION`, `protoCore_SOVERSION`, `protoCore_INCLUDE_DIR` from Task 1; `HKLM\SOFTWARE\protoCore\Soversion` from Task 1 Step 7.
- Produces: `PROTOCORE_LIBRARY` consumed at `CMakeLists.txt:171`; `PROTOCORE_INCLUDE_DIRS` consumed at `:196` and `:209`; a hand-built `protoJS_<version>_amd64.deb` whose `preinst` refuses a protoCore outside `[2.0.0, 3.0.0)`.

**Build parallelism reminder: no `-j` for any protoJS build, and no test262 sweep without asking the user.**

- [ ] **Step 1: Branch, and GitNexus impact for the source-touching part**

protoJS is indexed by GitNexus and its `CLAUDE.md` requires impact analysis before editing a symbol. This task edits only CMake, shell and template files, so no symbol is touched; run `gitnexus_detect_changes()` before committing, as that same file requires.

```bash
W=/home/gamarino/Documentos/proyectos
git -C $W/protoJS status --short
git -C $W/protoJS switch -c feature/phase-i-installers
```

The repository root also carries a committed build artefact, `protoJS/protoJS_0.1.0_amd64.deb`, and a leftover `protoJS/protoJS_staging/` directory. Do not delete them in this task — deleting committed artefacts is a maintainer decision, and Step 7 makes `build_deb.sh` write to a path that does not collide with them.

- [ ] **Step 2: Replace the protoCore discovery block**

Replace `protoJS/CMakeLists.txt:14-51` with:

```cmake
# --- protoCore -----------------------------------------------------------------
# Resolution order (Phase I, decision D-I1):
#   1. An installed protoCore CMake package: find_package(protoCore 2.0 CONFIG).
#      The only mode that checks protoCore's version and ABI, and therefore the
#      only mode a distributable package may be built in. Name a prefix with
#      -DPROTO_CORE_PREFIX=<prefix> or -DCMAKE_PREFIX_PATH=<prefix>.
#   2. Developer fallback: a sibling build directory, searched in the order
#      ../protoCore/build_release, ../protoCore/build, ../protoCore/build_check.
#      build_release comes first because it is the directory protoCore's own
#      release workflow writes to, so a developer who keeps a stale
#      ../protoCore/build around does not silently link protoJS against it.
#      protoJS does not build protoCore itself; it must already be built.
set(PROTOCORE_MIN_VERSION      "2.0")
set(PROTOCORE_MIN_VERSION_FULL "2.0.0")
set(PROTOCORE_NEXT_MAJOR       "3.0.0")
set(PROTOCORE_ABI_SOVERSION    "2")
option(PROTOCORE_REQUIRE_PACKAGE
    "Require an installed protoCore CMake package; never fall back to ../protoCore" OFF)

if(DEFINED PROTO_CORE_PREFIX)
    list(APPEND CMAKE_PREFIX_PATH "${PROTO_CORE_PREFIX}")
endif()

find_package(protoCore ${PROTOCORE_MIN_VERSION} QUIET CONFIG)

if(protoCore_FOUND)
    if(NOT protoCore_SOVERSION STREQUAL PROTOCORE_ABI_SOVERSION)
        message(FATAL_ERROR
            "protoCore package at ${protoCore_DIR} declares SOVERSION "
            "${protoCore_SOVERSION}; protoJS is built against SOVERSION "
            "${PROTOCORE_ABI_SOVERSION}. Install a matching protoCore.")
    endif()
    set(PROTOCORE_LIBRARY protoCore::protoCore)
    set(PROTOCORE_INCLUDE_DIRS "${protoCore_INCLUDE_DIR}")
    include_directories(${PROTOCORE_INCLUDE_DIRS})
    message(STATUS "protoJS: installed protoCore ${protoCore_VERSION} "
                   "(SOVERSION ${protoCore_SOVERSION}) from ${protoCore_DIR}")
else()
    set(_protoCore_where "")
    if(DEFINED PROTO_CORE_PREFIX)
        set(_protoCore_where " under ${PROTO_CORE_PREFIX}")
    endif()
    if(DEFINED PROTO_CORE_PREFIX OR PROTOCORE_REQUIRE_PACKAGE)
        message(FATAL_ERROR
            "No protoCore >= ${PROTOCORE_MIN_VERSION_FULL} CMake package was "
            "found${_protoCore_where}.\n"
            "  A prefix holding only libprotoCore and protoCore.h is no longer "
            "accepted: without lib/cmake/protoCore/protoCoreConfig.cmake there "
            "is no way to tell protoCore 1.x from 2.x. Install protoCore "
            "${PROTOCORE_MIN_VERSION_FULL} or later:\n"
            "    cmake -S ../protoCore -B ../protoCore/build_release "
            "-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=<prefix>\n"
            "    cmake --build ../protoCore/build_release --target protoCore\n"
            "    cmake --install ../protoCore/build_release --component protoCore")
    endif()
    unset(_protoCore_where)
    message(WARNING
        "protoJS: no installed protoCore package found; using the developer "
        "build in ../protoCore. This mode performs no package version check "
        "and must not be used to produce a distributable package (configure "
        "with -DPROTOCORE_REQUIRE_PACKAGE=ON to forbid it).")
    set(PROTOCORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../protoCore)
    find_library(PROTOCORE_LIBRARY NAMES protoCore
        PATHS ${PROTOCORE_DIR}/build_release ${PROTOCORE_DIR}/build ${PROTOCORE_DIR}/build_check
        NO_DEFAULT_PATH
    )
    if(NOT PROTOCORE_LIBRARY)
        message(FATAL_ERROR "protoCore shared library not found. Build protoCore first:\n  cd ${PROTOCORE_DIR} && cmake -B build_release -S . && cmake --build build_release --target protoCore")
    endif()
    get_filename_component(PROTOCORE_LIBRARY_DIR "${PROTOCORE_LIBRARY}" DIRECTORY)
    if(APPLE)
        set(_protoCore_abi_file "${PROTOCORE_LIBRARY_DIR}/libprotoCore.${PROTOCORE_ABI_SOVERSION}.dylib")
    else()
        set(_protoCore_abi_file "${PROTOCORE_LIBRARY_DIR}/libprotoCore.so.${PROTOCORE_ABI_SOVERSION}")
    endif()
    if(NOT EXISTS "${_protoCore_abi_file}")
        message(FATAL_ERROR
            "The protoCore build in ${PROTOCORE_LIBRARY_DIR} does not provide "
            "${_protoCore_abi_file}, so it is not SOVERSION "
            "${PROTOCORE_ABI_SOVERSION}. Rebuild ../protoCore from clean.")
    endif()
    unset(_protoCore_abi_file)
    set(PROTOCORE_INCLUDE_DIRS ${PROTOCORE_DIR} ${PROTOCORE_DIR}/headers)
    include_directories(${PROTOCORE_INCLUDE_DIRS})
    message(STATUS "Found protoCore: ${PROTOCORE_LIBRARY}")
    # Build-tree RPATH so protojs finds libprotoCore without LD_LIBRARY_PATH.
    set(CMAKE_BUILD_RPATH "${PROTOCORE_LIBRARY_DIR}")
    set(CMAKE_BUILD_RPATH_USE_LINK_PATH TRUE)
endif()
```

- [ ] **Step 3: Guard the CPack generators and version the dependency**

Replace `protoJS/CMakeLists.txt:295-312` with the same shape as Tasks 3 and 4, using the cache variable names `PROTOJS_DPKG_EXECUTABLE` and `PROTOJS_RPMBUILD_EXECUTABLE`, `set(CPACK_DEBIAN_PACKAGE_NAME "protojs")`, `set(CPACK_RPM_PACKAGE_NAME "protoJS")`, the two bounded dependency relations built from `PROTOCORE_MIN_VERSION_FULL` and `PROTOCORE_NEXT_MAJOR`, and the two `UNVERIFIED` comments on the `APPLE` and `WIN32` branches. Add one comment at the top of the block recording that this CPack configuration coexists with the hand-built `packaging/` pipeline and produces a differently named package:

```cmake
# Platform-specific generators.
#
# Note: this repository has two packaging paths. This CPack configuration
# produces a package named "protojs"; packaging/build_deb.sh produces one named
# "protoJS" from the templates under packaging/templates/. Both are supported
# (decision D-I4); they must not be installed at the same time.
```

- [ ] **Step 4: Raise the DEB control template's floor**

`protoJS/packaging/templates/linux/control.template:5` currently reads `Depends: protocore (>= 1.0.0)`. Replace with:

```
Depends: protocore (>= 2.0.0), protocore (<< 3.0.0)
```

- [ ] **Step 5: Rewrite the DEB `preinst` check**

Replace `protoJS/packaging/templates/linux/preinst.template:4-27` with the block below, keeping the surrounding `#!/bin/bash`, `set -e` and the trailing `exit 0`:

```bash
# protoCore's major version and its soname move together, so protoJS accepts
# exactly the 2.x series it was built against.
MIN_VERSION="2.0.0"
NEXT_MAJOR="3.0.0"
SONAME="libprotoCore.so.2"

PACKAGE_NAME=""
# CPack names protoCore's .deb "protocore"; older hand-built packages used
# "protoCore". Check both.
for pkg in protocore protoCore; do
    if dpkg-query -W -f='${Status}' "$pkg" 2>/dev/null | grep -q "ok installed"; then
        PACKAGE_NAME="$pkg"
        break
    fi
done

if [ -z "$PACKAGE_NAME" ]; then
    echo "ERROR: protoCore is not installed." >&2
    echo "protoJS requires protoCore >= $MIN_VERSION and < $NEXT_MAJOR." >&2
    exit 1
fi

echo "Checking for protoCore dependency..."
INSTALLED_VERSION=$(dpkg-query -W -f='${Version}' "$PACKAGE_NAME")

if dpkg --compare-versions "$INSTALLED_VERSION" lt "$MIN_VERSION"; then
    echo "ERROR: protoCore version $INSTALLED_VERSION is too old." >&2
    echo "protoJS requires protoCore >= $MIN_VERSION." >&2
    exit 1
fi

if dpkg --compare-versions "$INSTALLED_VERSION" ge "$NEXT_MAJOR"; then
    echo "ERROR: protoCore version $INSTALLED_VERSION is too new." >&2
    echo "protoJS is built against the $MIN_VERSION series and needs < $NEXT_MAJOR." >&2
    exit 1
fi

# The package version is metadata; the soname is what the loader will use.
if ! dpkg-query -L "$PACKAGE_NAME" 2>/dev/null | grep -q "/$SONAME\$"; then
    echo "ERROR: $PACKAGE_NAME $INSTALLED_VERSION does not provide $SONAME." >&2
    echo "protoJS was linked against $SONAME and will not start without it." >&2
    exit 1
fi

echo "Dependency check passed: $PACKAGE_NAME $INSTALLED_VERSION providing $SONAME."
```

- [ ] **Step 6: Raise the RPM spec's floor**

`protoJS/packaging/templates/linux/protoJS.spec.template:11` becomes:

```
Requires:       protoCore >= 2.0.0, protoCore < 3.0.0
```

and the `%pre` scriptlet at `:19-38` becomes:

```
%pre
MIN_VERSION="2.0.0"
NEXT_MAJOR="3.0.0"
PACKAGE_NAME="protoCore"

echo "Checking for $PACKAGE_NAME dependency..."

if ! rpm -q "$PACKAGE_NAME" > /dev/null 2>&1; then
    echo "ERROR: protoCore is not installed." >&2
    echo "protoJS requires protoCore >= $MIN_VERSION and < $NEXT_MAJOR." >&2
    exit 1
fi

INSTALLED_VERSION=$(rpm -q --queryformat '%{VERSION}' "$PACKAGE_NAME")

# Version comparison with sort -V, so rpmdev-vercmp is not required.
if [ "$(printf '%s\n%s' "$MIN_VERSION" "$INSTALLED_VERSION" | sort -V | head -n1)" != "$MIN_VERSION" ]; then
    echo "ERROR: protoCore version $INSTALLED_VERSION is too old." >&2
    echo "protoJS requires protoCore >= $MIN_VERSION." >&2
    exit 1
fi

if [ "$(printf '%s\n%s' "$NEXT_MAJOR" "$INSTALLED_VERSION" | sort -V | head -n1)" = "$NEXT_MAJOR" ]; then
    echo "ERROR: protoCore version $INSTALLED_VERSION is too new." >&2
    echo "protoJS is built against the $MIN_VERSION series and needs < $NEXT_MAJOR." >&2
    exit 1
fi

if ! rpm -ql "$PACKAGE_NAME" | grep -q '/libprotoCore\.so\.2$'; then
    echo "ERROR: $PACKAGE_NAME $INSTALLED_VERSION does not provide libprotoCore.so.2." >&2
    exit 1
fi
```

- [ ] **Step 7: Fix the macOS preinstall check**

`protoJS/packaging/templates/macos/preinstall.template:4-5` names `com.protoCore.pkg` and `/usr/local/lib/libprotoCore.dylib`. protoCore's macOS generator is `DragNDrop`, which produces a `.dmg` and registers no package id, so the `pkgutil` branch can never fire today; and the unversioned `.dylib` symlink says nothing about the ABI. Replace `:4-19` with:

```bash
# Configuration.
# protoCore's macOS packaging is currently DragNDrop (a .dmg), which registers
# no pkgutil receipt, so the pkgutil check below only succeeds once protoCore
# ships a productbuild .pkg with this identifier. The versioned dylib is the
# check that works today, and it is the one that actually tests the ABI: the
# unversioned libprotoCore.dylib symlink would be satisfied by any version.
PROTOCORE_PKG_ID="com.protoCore.pkg"
PROTOCORE_SOVERSION="2"
PROTOCORE_LIB_DIRS="/usr/local/lib /opt/homebrew/lib /opt/local/lib"

echo "Checking for protoCore dependency..."

for dir in $PROTOCORE_LIB_DIRS; do
    if [ -f "$dir/libprotoCore.$PROTOCORE_SOVERSION.dylib" ]; then
        echo "protoCore detected: $dir/libprotoCore.$PROTOCORE_SOVERSION.dylib"
        exit 0
    fi
done

if pkgutil --pkg-info "$PROTOCORE_PKG_ID" > /dev/null 2>&1; then
    echo "protoCore detected via pkgutil ($PROTOCORE_PKG_ID)."
    exit 0
fi
```

The final `echo "ERROR: protoCore is not installed." ... exit 1` at `:21-24` is unchanged except for naming the required ABI:

```bash
echo "ERROR: protoCore with ABI $PROTOCORE_SOVERSION is not installed." >&2
echo "Install protoCore 2.x before installing protoJS." >&2
exit 1
```

- [ ] **Step 8: Make the WiX condition testable**

`protoJS/packaging/templates/windows/protoJS.wxs.template:13-26` searches for `HKLM\SOFTWARE\protoCore\Version` and, as a fallback, for `protoCore.dll` in `[ProgramFiles64Folder]protoCore`. Task 1 Step 7 makes protoCore's NSIS installer write `Version`, `Soversion` and `InstallDir` under that key, so the search now has something to find. Replace `:11-26` with:

```xml
        <!-- protoCore dependency check.
             protoCore's NSIS installer writes Version, Soversion and InstallDir
             under HKLM\SOFTWARE\protoCore (protoCore/CMakeLists.txt, the WIN32
             CPack branch). A Windows DLL carries no soname, so Soversion is the
             only ABI statement available and is what this condition tests.
             UNVERIFIED: never built - no Windows host. -->
        <Property Id="PROTOCORE_SOVERSION">
            <RegistrySearch Id="ProtoCoreSoversionSearch" Root="HKLM"
                            Key="SOFTWARE\protoCore" Name="Soversion" Type="raw" />
        </Property>
        <Property Id="PROTOCORE_VERSION">
            <RegistrySearch Id="ProtoCoreVersionSearch" Root="HKLM"
                            Key="SOFTWARE\protoCore" Name="Version" Type="raw" />
        </Property>
        <Property Id="PROTOCORE_INSTALLDIR">
            <RegistrySearch Id="ProtoCoreInstallDirSearch" Root="HKLM"
                            Key="SOFTWARE\protoCore" Name="InstallDir" Type="raw" />
        </Property>

        <Condition Message="ERROR: protoCore with ABI version 2 was not found. Install protoCore 2.x before installing protoJS.">
            <![CDATA[Installed OR PROTOCORE_SOVERSION = "#2" OR PROTOCORE_SOVERSION = "2"]]>
        </Condition>
```

(Windows Installer prefixes a raw `REG_SZ` search result with `#` in some cases, which is why both spellings are accepted; the Windows host task verifies which one appears.)

- [ ] **Step 9: Make `build_deb.sh` use the release build and check the soname**

`protoJS/packaging/build_deb.sh:11-14` requires `build/protojs`, which is not the directory the repository actually builds into (`build_release`). Replace `:8-23` with:

```bash
: "${VERSION:=0.1.0}"
: "${MAINTAINER:=Gustavo Marino <gamarino@gmail.com>}"
: "${PROTOJS_BINARY:=build_release/protojs}"
: "${PROTOCORE_SONAME:=libprotoCore.so.2}"
: "${OUTDIR:=build_release}"

if [ ! -f "$PROTOJS_BINARY" ]; then
    echo "ERROR: $PROTOJS_BINARY not found. Build protoJS first:" >&2
    echo "  cmake -B build_release -S . -DCMAKE_BUILD_TYPE=Release && cmake --build build_release" >&2
    echo "  (no -j: parallel protoJS builds hang this machine)" >&2
    exit 1
fi

# The package's preinst requires protoCore's soname; refuse to build a package
# whose binary was linked against a different one.
if ! objdump -p "$PROTOJS_BINARY" | grep -q "NEEDED *$PROTOCORE_SONAME"; then
    echo "ERROR: $PROTOJS_BINARY is not linked against $PROTOCORE_SONAME." >&2
    objdump -p "$PROTOJS_BINARY" | grep "NEEDED *libprotoCore" >&2 || true
    echo "Rebuild protoJS against protoCore 2.x." >&2
    exit 1
fi

STAGING="$OUTDIR/protoJS_staging"
rm -rf "$STAGING"
mkdir -p "$STAGING/DEBIAN" "$STAGING/usr/bin"
cp "$PROTOJS_BINARY" "$STAGING/usr/bin/protojs"
chmod 755 "$STAGING/usr/bin/protojs"
sed -e "s/\${VERSION}/$VERSION/g" -e "s/\${MAINTAINER}/$MAINTAINER/g" \
    packaging/templates/linux/control.template > "$STAGING/DEBIAN/control"
cp packaging/templates/linux/preinst.template "$STAGING/DEBIAN/preinst"
chmod 755 "$STAGING/DEBIAN/preinst"
dpkg-deb --build "$STAGING" "$OUTDIR/protoJS_${VERSION}_amd64.deb"
```

Staging and output move under `build_release/`, so the script no longer writes `protoJS_staging/` and `protoJS_0.1.0_amd64.deb` into the repository root next to the committed artefacts of the same name.

- [ ] **Step 10: Configure, build (no `-j`) and package**

```bash
W=/home/gamarino/Documentos/proyectos
S=$W/.agent_scratch/phase-i-installers
rm -rf $W/protoJS/build_pkg
cmake -S $W/protoJS -B $W/protoJS/build_pkg -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH=$S/stage -DCMAKE_INSTALL_PREFIX=$S/stage \
    -DPROTOCORE_REQUIRE_PACKAGE=ON -DBUILD_TESTING=OFF 2>&1 \
    | tee $S/protojs-pkg-configure.txt | grep -E "protoJS:|CPack:"
cmake --build $W/protoJS/build_pkg          # deliberately no -j
(cd $W/protoJS/build_pkg && cpack -G DEB) 2>&1 | tail -3
objdump -p $W/protoJS/build_pkg/protojs | grep "NEEDED.*protoCore"
```
Expected: `protoJS: installed protoCore 2.0.0 (SOVERSION 2) from ...`; `CPack: RPM generator disabled (rpmbuild not found)`; `NEEDED libprotoCore.so.2`.

The hand-built path is exercised from a release build in the repository root:

```bash
W=/home/gamarino/Documentos/proyectos
S=$W/.agent_scratch/phase-i-installers
cd $W/protoJS && PROTOJS_BINARY=$W/protoJS/build_pkg/protojs OUTDIR=$W/protoJS/build_pkg \
    bash packaging/build_deb.sh 2>&1 | tee $S/protojs-build-deb.txt
dpkg -I $W/protoJS/build_pkg/protoJS_0.1.0_amd64.deb | grep Depends
dpkg-deb --fsys-tarfile $W/protoJS/build_pkg/protoJS_0.1.0_amd64.deb | tar tf - | head
dpkg-deb -I $W/protoJS/build_pkg/protoJS_0.1.0_amd64.deb preinst | grep -E "MIN_VERSION|NEXT_MAJOR|SONAME"
```
Expected: `Depends: protocore (>= 2.0.0), protocore (<< 3.0.0)`; `MIN_VERSION="2.0.0"`, `NEXT_MAJOR="3.0.0"`, `SONAME="libprotoCore.so.2"`.

- [ ] **Step 11: Documentation and commit**

Update `protoJS/docs/INSTALLATION.md` with the protoCore 2.x requirement, the two packaging paths and their different package names, and the platform table:

| Platform | Packaging | Status |
|----------|-----------|--------|
| Linux | CPack TGZ/DEB/RPM as `protojs`; `packaging/build_deb.sh` as `protoJS` | Both built; the hand-built `.deb` was extracted and smoke-tested |
| macOS | `packaging/templates/macos/preinstall.template` | Configured and reviewed, **never built** — no macOS host |
| Windows | `packaging/templates/windows/protoJS.wxs.template` (WiX v3) | Configured and reviewed, **never built** — no Windows host |

```bash
W=/home/gamarino/Documentos/proyectos
git -C $W/protoJS add CMakeLists.txt packaging/build_deb.sh \
    packaging/templates/linux/control.template \
    packaging/templates/linux/preinst.template \
    packaging/templates/linux/protoJS.spec.template \
    packaging/templates/macos/preinstall.template \
    packaging/templates/windows/protoJS.wxs.template \
    docs/INSTALLATION.md
git -C $W/protoJS commit -m "$(cat <<'EOF'
Require protoCore 2.x in the build and in every packaging template

Prefer find_package(protoCore 2.0 CONFIG) over the version-blind find_library
search, keeping the sibling ../protoCore build directories as an explicit
developer fallback that warns, asserts SOVERSION 2 and can be forbidden with
-DPROTOCORE_REQUIRE_PACKAGE=ON. Guard the DEB and RPM CPack generators on dpkg
and rpmbuild. Raise every hardcoded protoCore 1.0.0 check in packaging/ to the
[2.0.0, 3.0.0) range and back it with a libprotoCore.so.2 soname check; the
macOS template now tests the versioned dylib and the WiX template tests the
Soversion registry value protoCore's NSIS installer writes. build_deb.sh builds
from build_release and stages inside it.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

**Done-when:**

```bash
W=/home/gamarino/Documentos/proyectos
! grep -rn "1\.0\.0" $W/protoJS/packaging/templates/ \
  && dpkg -I $W/protoJS/build_pkg/protoJS_0.1.0_amd64.deb | grep -q "protocore (>= 2.0.0), protocore (<< 3.0.0)" \
  && objdump -p $W/protoJS/build_pkg/protojs | grep -q "NEEDED *libprotoCore.so.2" \
  && echo "TASK 6 DONE"
```
prints `TASK 6 DONE`. (The first clause requires that no `1.0.0` string survives anywhere under `packaging/templates/`; the RPM spec's `%changelog` entry mentions `0.1.0`, not `1.0.0`, so it does not trip it.)

---

### Task 7: protoST — executable self-location on three platforms (D-I5)

**Files:**
- Modify: `protoST/src/runtime/STRuntime.cpp` (platform includes near `:24-40`; `discoverStdlibDir()` at `:1946-1999`)
- Modify: `protoST/docs/INSTALLATION.md` (created in Task 3)

**Interfaces:**
- Consumes: nothing from the other tasks; it is independent of protoCore's package and could be done first. It is placed here because its verification runs against the installed layout Task 3 produces.
- Produces: `static std::string executablePath()` in the anonymous/static scope of `STRuntime.cpp`, returning the absolute path of the running executable on Linux, macOS and Windows, or `""`; `discoverStdlibDir()` step 2 then works on all three.

- [ ] **Step 1: GitNexus impact is not required here**

protoST is not one of the GitNexus-indexed repositories (the workspace `CLAUDE.md` names protoJS and protoPython). `discoverStdlibDir` is a file-static function with one caller; confirm that before editing:

```bash
W=/home/gamarino/Documentos/proyectos
grep -rn "discoverStdlibDir" $W/protoST/src/
```
Expected: the definition plus exactly one call site. If there is more than one caller, read each before changing the function's behaviour.

- [ ] **Step 2: Add the platform includes**

`protoST/src/runtime/STRuntime.cpp` includes `<filesystem>` at `:32` and no platform headers. Add, after the standard-library include block that ends around `:40`:

```cpp
// Executable self-location (see executablePath() below). protoPython does the
// same thing in src/runtime/main.cpp; protoST needs it so an installed binary
// can find share/protoST/lib on macOS and Windows, not only on Linux.
#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <climits>
#include <mach-o/dyld.h>
#else
#include <climits>
#include <unistd.h>
#endif
```

- [ ] **Step 3: Add `executablePath()` immediately above `discoverStdlibDir()`**

Insert before the comment block at `:1946`:

```cpp
// Absolute path of the running executable, or "" when it cannot be determined.
//
// Ported from protoPython's getExecutablePath() (protoPython/src/runtime/
// main.cpp:38-54) with two corrections: a Linux readlink() that exactly fills
// the buffer may have truncated the path, so that result is rejected rather
// than returned as if it were complete; and _NSGetExecutablePath may hand back
// a path containing "." or ".." components or an unresolved symlink, so the
// caller normalises what it gets.
static std::string executablePath() {
#if defined(__linux__)
    char buffer[PATH_MAX];
    const ssize_t count = ::readlink("/proc/self/exe", buffer, sizeof(buffer));
    if (count > 0 && count < static_cast<ssize_t>(sizeof(buffer)))
        return std::string(buffer, static_cast<std::size_t>(count));
#elif defined(_WIN32)
    char buffer[MAX_PATH];
    const DWORD count = ::GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    if (count > 0 && count < MAX_PATH)
        return std::string(buffer, static_cast<std::size_t>(count));
#elif defined(__APPLE__)
    char buffer[PATH_MAX];
    uint32_t size = sizeof(buffer);
    if (::_NSGetExecutablePath(buffer, &size) == 0)
        return std::string(buffer);
#endif
    return "";
}
```

- [ ] **Step 4: Rewrite step 2 of `discoverStdlibDir()`**

Replace the comment at `:1951-1956` and the block at `:1971-1989` with:

```cpp
//   2. Derived from the executable's own location, on Linux, macOS and
//      Windows: probe <dir-of-exe>/lib, <dir-of-exe>/../lib,
//      <dir-of-exe>/../../lib, and the installed layouts
//      <dir-of-exe>/../share/protoST/lib and <dir-of-exe>/share/protoST/lib.
//      A dev build runs build/protost, so "../lib" resolves the project lib/;
//      an installed bin/protost finds <prefix>/share/protoST/lib (where the
//      install rules place the stdlib .st modules); the last candidate covers
//      a flat Windows install directory.
```

```cpp
    // 2. Derived from the executable location (Linux, macOS and Windows).
    {
        const std::string exeString = executablePath();
        if (!exeString.empty()) {
            std::error_code ec;
            fs::path exe = fs::weakly_canonical(fs::path(exeString), ec);
            if (ec) exe = fs::path(exeString);
            const fs::path dir = exe.parent_path();
            const fs::path candidates[] = {
                dir / "lib",
                dir.parent_path() / "lib",
                dir.parent_path().parent_path() / "lib",
                // Installed layout: <prefix>/bin/protost -> <prefix>/share/protoST/lib.
                dir.parent_path() / "share" / "protoST" / "lib",
                // Flat install directory (the Windows NSIS/ZIP layout).
                dir / "share" / "protoST" / "lib",
            };
            for (const auto& c : candidates) {
                std::error_code probe;
                if (fs::is_directory(c, probe)) return c.string();
            }
        }
    }
```

Note the separate `probe` error code inside the loop: the original reused the outer `ec`, so once one `is_directory` call set it, later calls carried a stale error value. It never changed the result (`is_directory` returns `false` on error either way) but it makes the loop's behaviour depend on a variable it does not own.

- [ ] **Step 5: Rebuild and re-run the suite**

```bash
W=/home/gamarino/Documentos/proyectos
S=$W/.agent_scratch/phase-i-installers
cmake --build $W/protoST/build_pkg -j4
ctest --test-dir $W/protoST/build_pkg --output-on-failure 2>&1 | tail -15 | tee $S/protost-ctest-after-selflocation.txt
diff <(tail -3 $S/protost-ctest.txt) <(tail -3 $S/protost-ctest-after-selflocation.txt)
```
Expected: no difference from the Task 3 baseline. The Linux code path is behaviourally identical except for the truncation rejection and the normalisation, so any change here is a regression.

- [ ] **Step 6: Prove the install-relative lookup works, with a negative control**

```bash
W=/home/gamarino/Documentos/proyectos
S=$W/.agent_scratch/phase-i-installers
STAGE=$S/stage
cmake --install $W/protoST/build_pkg --component protoST
mkdir -p $S/run && cp $W/protoST/tests/conformance/13-stdlib/stream-write-nextput.st $S/run/
cd $S/run && env -u PROTOST_LIB -u LD_LIBRARY_PATH $STAGE/bin/protost stream-write-nextput.st; echo "exit=$?"
# Negative control: hide the installed stdlib and require a failure.
mv $STAGE/share/protoST $STAGE/share/protoST.hidden
cd $S/run && env -u PROTOST_LIB -u LD_LIBRARY_PATH $STAGE/bin/protost stream-write-nextput.st; echo "exit=$?"
mv $STAGE/share/protoST.hidden $STAGE/share/protoST
```
Expected: `exit=0` the first time (the `Import from: 'stream'` resolved out of `$STAGE/share/protoST/lib`, reached through `<dir-of-exe>/../share/protoST/lib`), and a non-zero exit the second time. A zero exit in the negative control means the lookup found the stdlib somewhere else — most likely `<cwd>/lib` — and the positive result proves nothing.

- [ ] **Step 7: Documentation and commit**

In `protoST/docs/INSTALLATION.md`, state in the stdlib-discovery section that the executable-derived step works on Linux, macOS and Windows, that the Linux path was verified by the negative control above, and that the macOS and Windows branches are compiled from the same code but **never run here** (D-I6).

```bash
W=/home/gamarino/Documentos/proyectos
git -C $W/protoST add src/runtime/STRuntime.cpp docs/INSTALLATION.md
git -C $W/protoST commit -m "$(cat <<'EOF'
Locate the standard library from the executable on macOS and Windows too

discoverStdlibDir() derived its candidate paths from /proc/self/exe, so on
macOS and Windows the whole step was skipped and an installed protost could
not find share/protoST/lib without PROTOST_LIB. Add executablePath() with the
three platform implementations, ported from protoPython's getExecutablePath()
and hardened against a truncated readlink and a non-canonical
_NSGetExecutablePath result, and add a flat-install candidate for the Windows
layout. The macOS and Windows branches are unverified: no such host.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

**Done-when:**

```bash
W=/home/gamarino/Documentos/proyectos
S=$W/.agent_scratch/phase-i-installers
grep -q "_NSGetExecutablePath" $W/protoST/src/runtime/STRuntime.cpp \
  && grep -q "GetModuleFileNameA" $W/protoST/src/runtime/STRuntime.cpp \
  && (cd $S/run && env -u PROTOST_LIB $S/stage/bin/protost stream-write-nextput.st >/dev/null 2>&1) \
  && echo "TASK 7 DONE"
```
prints `TASK 7 DONE`.

---

### Task 8: Verification — build, install and smoke-test every Linux package in a scratch prefix

**Files:** none created in any repository. All output goes to `/home/gamarino/Documentos/proyectos/.agent_scratch/phase-i-installers/`.

**Interfaces:**
- Consumes: everything Tasks 1-7 produced.
- Produces: `$S/verification-report.txt`, a single file recording each command, its expected result and its actual result.

- [ ] **Step 1: Fresh scratch roots**

```bash
W=/home/gamarino/Documentos/proyectos
S=$W/.agent_scratch/phase-i-installers
rm -rf $S/stage $S/debroot && mkdir -p $S/stage $S/debroot $S/run
```
protoCore and every runtime is then reinstalled into the clean `$S/stage` by re-running the install step of each task (Task 1 Step 8; `cmake --install <build_pkg> --component <component>` for each runtime). Never pass a prefix outside `$S`.

- [ ] **Step 2: Install all six into the stage prefix**

```bash
W=/home/gamarino/Documentos/proyectos
S=$W/.agent_scratch/phase-i-installers
cmake --install $W/protoCore/build_release   --component protoCore
cmake --install $W/protoPython/build_pkg     --component protoPython
cmake --install $W/protoST/build_pkg         --component protoST
cmake --install $W/protoClojure/build_pkg    --component protoClojure
cmake --install $W/protoScala/build_pkg      --component protoScala
cmake --install $W/protoJS/build_pkg         --component protoJS
find $S/stage -maxdepth 3 | sort | tee $S/stage-layout.txt
```
Expected: `bin/{protopy,protopyc,protojs,protost,protoclj,protoscala}`; `lib/libprotoCore.so*`, `lib/libprotoPython.so*`, `lib/cmake/protoCore/`, `lib/pkgconfig/protoCore.pc`, `lib/protoPython/python3.14/`; `share/protoST/lib/*.st`, `share/protoClojure/examples/`, `share/doc/`.

- [ ] **Step 3: Every binary resolves `libprotoCore.so.2` inside the stage prefix**

```bash
S=/home/gamarino/Documentos/proyectos/.agent_scratch/phase-i-installers
for b in protopy protopyc protojs protost protoclj protoscala; do
    printf '%-12s ' "$b"
    env -u LD_LIBRARY_PATH ldd $S/stage/bin/$b | grep libprotoCore || echo "NO libprotoCore"
done | tee $S/ldd-report.txt
```
Expected: every line reads `libprotoCore.so.2 => <S>/stage/lib/libprotoCore.so.2`. A line resolving to `/usr/local/lib/libprotoCore.so.1` is the exact failure D-I7 is about and stops the task; a `not found` means the install RPATH (`$ORIGIN/../lib`) is wrong.

- [ ] **Step 4: Each runtime does real work from the stage prefix**

```bash
S=/home/gamarino/Documentos/proyectos/.agent_scratch/phase-i-installers
cd $S/run
printf 'console.log(JSON.stringify({ok: 1 + 1}));\n'     > hello.js
printf '(println (reduce + (range 1 11)))\n'             > hello.clj
printf '// EXPECT: 55\n@main def run(): Unit =\n  println((1 to 10).sum)\n' > hello.scala
cp /home/gamarino/Documentos/proyectos/protoST/tests/conformance/13-stdlib/stream-write-nextput.st .

env -u LD_LIBRARY_PATH $S/stage/bin/protopy -c "import json; print(json.dumps({'ok': 2}))"
env -u LD_LIBRARY_PATH $S/stage/bin/protojs hello.js
env -u LD_LIBRARY_PATH -u PROTOST_LIB $S/stage/bin/protost stream-write-nextput.st
env -u LD_LIBRARY_PATH $S/stage/bin/protoclj hello.clj
env -u LD_LIBRARY_PATH $S/stage/bin/protoscala hello.scala
```
Expected: `{"ok": 2}` (which proves the installed `lib/protoPython/python3.14` tree was found through the relative `STDLIB_PATH`); `{"ok":2}`; exit status 0 for protost (the `Import from: 'stream'` resolved out of `share/protoST/lib`); `55`; `55`. Record every line in `$S/verification-report.txt`.

- [ ] **Step 5: Build the Linux packages**

```bash
W=/home/gamarino/Documentos/proyectos
S=$W/.agent_scratch/phase-i-installers
for d in protoCore/build_release protoPython/build_pkg protoST/build_pkg protoClojure/build_pkg protoScala/build_pkg protoJS/build_pkg; do
    (cd $W/$d && cpack -G DEB 2>&1 | tail -2)
done | tee $S/cpack-deb.txt
ls $W/protoCore/build_release/*.deb $W/proto*/build_pkg/*.deb
```
Expected: six `.deb` files, and **no** attempt to run `rpmbuild` — the `CPack: RPM generator disabled (rpmbuild not found)` message from each configure is what guarantees it. Before D-I2, this loop fails on protoPython, protoST, protoClojure and protoJS.

- [ ] **Step 6: Extract every package into one root and smoke-test the package payload**

```bash
W=/home/gamarino/Documentos/proyectos
S=$W/.agent_scratch/phase-i-installers
rm -rf $S/debroot && mkdir -p $S/debroot
for f in $W/protoCore/build_release/*.deb $W/proto*/build_pkg/*.deb; do
    echo "== $f"; dpkg -I "$f" | grep -E "Package:|Version:|Depends:"
    dpkg-deb -x "$f" $S/debroot
done | tee $S/deb-metadata.txt
find $S/debroot -maxdepth 4 | sort | tee $S/debroot-layout.txt
cd $S/run
for b in protopy protojs protost protoclj protoscala; do
    printf '%-12s ' "$b"; env -u LD_LIBRARY_PATH ldd $S/debroot/usr/bin/$b | grep -c "libprotoCore.so.2 => $S/debroot/usr/lib"
done
env -u LD_LIBRARY_PATH $S/debroot/usr/bin/protopy -c "import json; print(json.dumps({'deb': 1}))"
env -u LD_LIBRARY_PATH -u PROTOST_LIB $S/debroot/usr/bin/protost stream-write-nextput.st; echo "protost exit=$?"
env -u LD_LIBRARY_PATH $S/debroot/usr/bin/protoscala hello.scala
```
Expected: every `Depends:` line names `protocore (>= 2.0.0), protocore (<< 3.0.0)`; every `ldd` count is `1`; `{"deb": 1}`; `protost exit=0`; `55`. This is the strongest available proof without `sudo`: the binaries run from the package's real `/usr/bin`, find `libprotoCore.so.2` in the package's real `/usr/lib` through `$ORIGIN/../lib`, and find their data files at the package's real `/usr/lib/protoPython/python3.14` and `/usr/share/protoST/lib`.

- [ ] **Step 7: Check the `preinst` of protoJS's hand-built package refuses a wrong protoCore**

```bash
W=/home/gamarino/Documentos/proyectos
S=$W/.agent_scratch/phase-i-installers
dpkg-deb -I $W/protoJS/build_pkg/protoJS_0.1.0_amd64.deb preinst > $S/protojs-preinst.sh
bash -n $S/protojs-preinst.sh && echo "preinst: syntax OK"
grep -E 'MIN_VERSION=|NEXT_MAJOR=|SONAME=' $S/protojs-preinst.sh
```
Expected: `preinst: syntax OK`, `MIN_VERSION="2.0.0"`, `NEXT_MAJOR="3.0.0"`, `SONAME="libprotoCore.so.2"`. The script is not executed: it queries the system package database, which is not this plan's business.

- [ ] **Step 8: D-I7 — prove `/usr/local` is now refused**

```bash
W=/home/gamarino/Documentos/proyectos
S=$W/.agent_scratch/phase-i-installers
ls -l /usr/local/lib/libprotoCore.so* /usr/local/include/protoCore.h    # read-only: confirm it is untouched
rm -rf $S/usrlocal-check
cmake -S $W/protoScala -B $S/usrlocal-check -DPROTO_CORE_PREFIX=/usr/local 2>&1 \
    | tee $S/usrlocal-refusal.txt | tail -20
```
Expected: a CMake `FATAL_ERROR` from protoScala saying that no protoCore ≥ 2.0.0 CMake package was found **under /usr/local**, and explaining that a prefix holding only a library and a header is no longer accepted. Before this phase the same command configures successfully and links protoScala against `libprotoCore.so.1`. Confirm with the first command that `/usr/local` still holds exactly `libprotoCore.so`, `libprotoCore.so.1`, `libprotoCore.so.1.0.0` and `protoCore.h`, all unmodified.

- [ ] **Step 9: Confirm nothing escaped the workspace**

```bash
W=/home/gamarino/Documentos/proyectos
find / -xdev -newermt '-1 day' -not -path "$W/*" -not -path '/proc/*' -not -path '/sys/*' \
     -not -path '/run/*' -not -path '/tmp/*' -not -path "$HOME/.cache/*" -type f 2>/dev/null | head -20
git -C $W/protoCore status --short; git -C $W/protoPython status --short
git -C $W/protoST status --short; git -C $W/protoClojure status --short
git -C $W/protoScala status --short; git -C $W/protoJS status --short
```
Expected: nothing outside the workspace, and six clean working trees (build directories are ignored; if any repository's `.gitignore` does not cover `build_pkg/`, leave the directory untracked rather than adding an ignore rule in this phase).

- [ ] **Step 10: Write the verification report**

`$S/verification-report.txt` records, in this order: the six package file names with their `Package:`, `Version:` and `Depends:` lines; the `ldd` table for the stage prefix and for the extracted package root; each runtime's smoke-test output; the protoST negative control from Task 7 Step 6; the `/usr/local` refusal from Step 8; and the list of what was **not** verified (every macOS and every Windows artefact).

**Done-when:**

```bash
S=/home/gamarino/Documentos/proyectos/.agent_scratch/phase-i-installers
[ "$(grep -c 'protocore (>= 2.0.0), protocore (<< 3.0.0)' $S/deb-metadata.txt)" = "5" ] \
  && [ "$(grep -c "libprotoCore.so.2 => $S/stage/lib" $S/ldd-report.txt)" = "6" ] \
  && grep -q "No protoCore >= 2.0.0 CMake package was found under /usr/local" $S/usrlocal-refusal.txt \
  && echo "TASK 8 DONE"
```
prints `TASK 8 DONE`. (Five `Depends:` lines, not six: protoCore's own package depends on nothing of the sort.)

---

## What a human must do on a macOS host

None of the following was run. Each item names the file it exercises and what a pass looks like.

1. **Build and install protoCore.** `cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$HOME/protocore-test && cmake --build build --target protoCore && cmake --install build --component protoCore`. Confirm `lib/libprotoCore.2.dylib`, `lib/cmake/protoCore/protoCoreConfig.cmake` and `lib/pkgconfig/protoCore.pc` exist, and that `pkg-config --variable=soversion protoCore` prints `2`. The config file's macOS ABI branch (`libprotoCore.${protoCore_SOVERSION}.dylib`, Task 1 Step 4) has never been evaluated on a real dylib name — verify the filename CMake actually produces and correct the branch if it differs.
2. **Build each runtime against it** with `-DCMAKE_PREFIX_PATH=$HOME/protocore-test -DPROTOCORE_REQUIRE_PACKAGE=ON`, then `otool -L` each installed binary and confirm the `@executable_path/../lib` RPATH resolves `libprotoCore.2.dylib`. The install RPATH is set for macOS in all five runtimes (`protoPython/CMakeLists.txt:1014`, `protoJS/:220-221`, `protoST/:172-173`, `protoClojure/:136-137`, `protoScala/:180-181`) and has never been exercised.
3. **Run `cpack -G DragNDrop`** in each runtime's build directory and mount each `.dmg`. CPack's DragNDrop generator is configured in all five and has never been run anywhere in this family.
4. **Run protoST's stdlib lookup from the mounted image**, with `PROTOST_LIB` unset: `./protost <a script using Import from: 'stream'>`. This is the only test of the `_NSGetExecutablePath` branch added in Task 7.
5. **Decide protoCore's macOS package format.** `packaging/templates/macos/preinstall.template` can only use `pkgutil` if protoCore ships a `productbuild` `.pkg` registering `com.protoCore.pkg`; today it ships a `.dmg`, which registers nothing. Either add the `productbuild` generator to protoCore's `APPLE` branch and keep the pkgutil check, or accept the versioned-dylib check as the only one and delete the pkgutil branch. The template as edited in Task 6 works either way.
6. **Report which of the five runtimes' `.dmg` files actually install a usable binary**, so the "Platform verification status" tables in the six installation documents can be updated from "never built" to a real status.

## What a human must do on a Windows host

1. **Read protoCore's own warning first.** `protoCore/docs/INSTALLATION.md:12` records that the cell allocator calls `posix_memalign` and that `CMakeLists.txt` adds the GCC/Clang option `-fno-delete-null-pointer-checks` unconditionally, so a native MSVC build is not expected to work without source changes. Establish whether the target is MSVC, MinGW or MSYS2 before anything else; the rest of this list assumes protoCore builds at all.
2. **Build and install protoCore, then run `cpack -G NSIS`.** Install the resulting `.exe` and check that `HKLM\SOFTWARE\protoCore` holds `Version`, `Soversion` and `InstallDir` (Task 1 Step 7). The NSIS escaping in `CPACK_NSIS_EXTRA_INSTALL_COMMANDS` — single-quoted strings, `\\` for the registry path separator — has never been through a real `makensis` run and is the most likely thing to need adjusting.
3. **Confirm which spelling `RegistrySearch` returns.** The WiX condition in Task 6 Step 8 accepts both `"2"` and `"#2"` because Windows Installer prefixes some raw `REG_SZ` results. Find out which one appears and simplify the condition.
4. **Build the protoJS MSI** from `packaging/templates/windows/protoJS.wxs.template` (replace the three `PUT-GUID-HERE-*` placeholders with real GUIDs first — they are still in the file and the plan deliberately does not invent them), install it without protoCore present and confirm the blocking condition fires, then install protoCore and confirm the MSI proceeds.
5. **Run `cpack -G NSIS` for protoPython, protoST, protoClojure and protoScala**, and check that each installed binary finds `protoCore.dll`. On Windows the loader searches the executable's own directory, so there is no RPATH: the four runtimes' NSIS packages install to their own directory and protoCore installs to its own. Determine whether `PATH` must include protoCore's `bin`, and document the answer in each repository's installation document.
6. **Run protoST from the installed location with `PROTOST_LIB` unset.** This is the only test of the `GetModuleFileNameA` branch added in Task 7 and of the flat-install candidate `<dir-of-exe>/share/protoST/lib` added in the same step.
7. **Run protoPython's `protopy -c "import json; print(json.dumps({}))"`** from the installed location. The relative `STDLIB_PATH` is computed with `file(RELATIVE_PATH ...)` from `CMAKE_INSTALL_FULL_BINDIR`; on a Windows install layout the result may not be `../lib/protoPython/python3.14`, and `protopyc`'s baked-in `PROTOPYC_INSTALL_*_DIRS` have the same exposure.

---

## Risk register

| # | Risk | Why it is plausible here | Mitigation in this plan | Residual |
|---|---|---|---|---|
| R1 | **A runtime built against 2.0.0 loads the 1.0.0 at run time.** | `/usr/local/lib` is in the default loader path on this machine and holds `libprotoCore.so.1`. The install RPATH `$ORIGIN/../lib` is searched *before* the default path, so this only bites when the runtime is installed to a prefix that has no `libprotoCore.so.2` — exactly what happens if protoCore's package is installed to prefix A and the runtime to prefix B. | Task 8 Step 3 requires `ldd` to show the resolution coming from inside the stage prefix for all six binaries, and Step 6 repeats it for the extracted package root. The DEB `Depends:` relation puts both packages under `/usr`, which is one prefix. | Two packages installed to different prefixes by hand remain unprotected. The soname difference (`.so.2` vs `.so.1`) makes the failure a load-time error, not silent corruption. |
| R2 | **An installed package and a sibling build are both present**, and the wrong one wins. | Every developer here has `../protoCore/build_release` populated, and Task 1 will also put a package in a prefix. | The order is explicit and documented: `find_package` first, sibling only when the package was not found and no prefix was named. The sibling branch emits `message(WARNING)`, and `-DPROTOCORE_REQUIRE_PACKAGE=ON` makes it a hard error for packaging builds. Task 2 Step 5 configures both ways and checks the message. | A developer who *wants* the sibling build while a package happens to be installed must now pass `-DCMAKE_IGNORE_PATH` or uninstall. This is a deliberate trade: the package winning by default is what makes packaging builds reproducible. |
| R3 | **A stale CMake cache mixes the two modes.** `PROTOCORE_LIBRARY` is a `find_library` cache entry in fallback mode and a plain variable holding `protoCore::protoCore` in package mode. Reconfiguring an existing build directory after switching modes leaves the cache entry behind. | The normal variable shadows the cache entry within the directory scope, so the *current* configure is correct — but `cmake -LA` will show a confusing leftover, and a future edit that reads the cache directly would get the wrong value. | Every task configures into a **fresh** `build_pkg` directory (`rm -rf` first). The installation documents tell users to delete the build directory when switching modes. | A user who reconfigures in place gets a correct build with a misleading cache. Not fixed here; fixing it properly means renaming the cache entry, which touches five repositories' documented `-DPROTOCORE_LIBRARY=<path>` escape hatch. |
| R4 | **`RPATH` is wrong after install.** All five runtimes set `INSTALL_RPATH` to `$ORIGIN/../${CMAKE_INSTALL_LIBDIR}` (Linux) or `@executable_path/../${CMAKE_INSTALL_LIBDIR}` (macOS), which assumes protoCore shares the prefix. protoPython additionally sets `@loader_path` / `$ORIGIN` for `libprotoPython`. | On a distribution where `CMAKE_INSTALL_LIBDIR` is `lib64` or a multiarch triplet, the two packages must agree on it; CPack DEB's default `/usr` prefix and the configure-time prefix must also agree. | Task 8 Steps 3 and 6 check the resolution from both the stage prefix and the extracted package root, which are different prefixes with the same relative layout. | A distribution with `lib64` has not been tested. The check to add on such a host is one `ldd` line. |
| R5 | **Data-file paths differ between the build tree and the install tree.** protoPython bakes a *relative* `STDLIB_PATH` computed from `CMAKE_INSTALL_FULL_BINDIR` to `<prefix>/lib/protoPython/python3.14` at configure time; protopyc bakes `PROTOPYC_INSTALL_INCLUDE_DIRS` and `PROTOPYC_INSTALL_LIBRARY_DIRS` the same way. If the configure-time prefix and the packaging prefix have different `bin`→`lib` relationships, the baked path is wrong in the package even though it is right in the stage install. | Task 8 Step 6 runs `protopy` **from the extracted package root** (`/usr/bin`, `/usr/lib`), not only from the stage prefix, and requires a real `import json` to succeed. That is the case where the two prefixes differ. | protopyc's generated Makefile paths are not exercised by a smoke test; `PROTOPYC_INSTALL_*` correctness in the package is assumed from the same relative-path arithmetic. |
| R6 | **protoST's new self-location changes Linux behaviour.** | The rewritten step 2 adds a `weakly_canonical` call and a fifth candidate, and rejects a truncated `readlink`. A symlinked `protost` now resolves through the symlink, where it previously used the raw `/proc/self/exe` value (which is itself already resolved, so this should be a no-op — but "should be" is the risk). | Task 7 Step 5 diffs the full ctest summary against the Task 3 baseline, and Step 6 adds a positive test plus a negative control that must fail. | A layout where the *old* code found a directory the new code does not is not enumerable; the ctest suite is the coverage. |
| R7 | **protoScala's `ProtoMPSCQueue` probe reads a different header than before.** The probe greps whichever `protoCore.h` it finds first in `PROTOCORE_INCLUDE_DIRS` for `newMPSCQueue` and flips `PROTOSCALA_HAS_PMQ`. In package mode that is now the *installed* header, not `../protoCore/headers`. | A concurrent plan is adding `newMPSCQueue` to protoCore. During the window where the source tree has it and an installed package does not (or vice versa), protoScala's actor mailbox implementation depends on which mode it was configured in. | Task 5 Step 3 makes both status messages print the absolute path of the header that was read, so a surprising mailbox backend is one `grep` away from explained. | The probe remains a substring search on header text. Replacing it with a `protoCore_VERSION VERSION_GREATER_EQUAL 2.1` check is the right fix and belongs to whichever phase lands `newMPSCQueue`. |
| R8 | **Two protoJS `.deb` packages exist with different names.** CPack produces `protojs`; `packaging/build_deb.sh` produces `protoJS`. dpkg treats them as unrelated packages and will happily install both, leaving two `/usr/bin/protojs` claims. | D-I4 keeps both deliberately. | Task 6 Step 3 adds a comment in `CMakeLists.txt` saying the two must not be installed together, and Task 6 Step 11 says so in `docs/INSTALLATION.md`. Task 6 Step 9 moves the hand-built output into `build_release/` so the two do not even share a directory. | Nothing prevents a user from installing both. Retiring one is a maintainer decision. |
| R9 | **`CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON` (protoScala only) pulls in a dependency on a package that does not exist.** `dpkg-shlibdeps` maps each `NEEDED` soname to the distribution package that owns it. `libprotoCore.so.2` is owned by no distribution package, so it may emit a warning or a bogus relation. | Enabled in Task 5 Step 4 for `libreadline` and `libstdc++`. | Task 5 Step 5 prints the full `dpkg -I` output and the done-when requires the `protocore` relation to be present and correct. If `shlibdeps` fails or adds noise, drop the line — the explicit `protocore` relation is the one that matters and the readline dependency can be named by hand. | Only protoScala enables it; the other repositories keep the current behaviour. |
| R10 | **The `1.0.0` grep in Task 6's done-when is too broad or too narrow.** | `! grep -rn "1\.0\.0" packaging/templates/` would also trip on an unrelated future `1.0.0`. | The clause is stated with its own reasoning in the done-when, and the templates were read in full while writing this plan: after Task 6 the only version strings left are `2.0.0`, `3.0.0`, `0.1.0` and `0.1.0.0`. | If a maintainer adds an unrelated `1.0.0` later, the check reports a false positive. It is a one-time verification, not a CI gate. |

---

## Self-review against the survey (done while writing; gaps fixed inline)

Every install rule, generator and discovery mechanism the survey found is accounted for below. Items marked *no change* are deliberate.

- **protoCore `install(TARGETS ... EXPORT protoCoreTargets ...)` (`:209-216`)** — kept as is; Task 1 Step 6 adds the missing `install(EXPORT)` that makes the `EXPORT` keyword mean something. **`PUBLIC_HEADER DESTINATION`** — no change.
- **protoCore CPack generator selection (`:229-247`)** — the Linux branch is the pattern the four runtimes copy (D-I2); the `WIN32` branch is extended with the registry commands (Task 1 Step 7); the `APPLE` branch is unchanged, and the macOS section above records why it may need to become `productbuild`.
- **protoCore `CPACK_COMPONENTS_ALL protoCore` (`:250`)** — no change; the four new install rules use the same component so the CMake package files and the `.pc` are packaged automatically.
- **protoCore `package_protocore_only` (`:262-272`)** — *no change*, and recorded in "Recorded, not asked" as producing an unusable library layout and colliding with the CPack TGZ filename. It is not on any installer path.
- **protoCore benchmark executables** — *no change*; none of them has an install rule, which is correct.
- **protoPython `install(TARGETS protopy protoPython protopyc ...)` (`:1027-1032`), `install(DIRECTORY include/protoPython ...)` (`:1034-1037`), `install(DIRECTORY lib/python3.14 ...)` (`:1039-1043`)** — *no change*; Task 8 Steps 4 and 6 verify the stdlib tree is found from both the stage prefix and the package root.
- **protoPython install RPATH (`:1011-1022`)** — *no change*; verified in Task 8 Step 3 (`_rpath_exe` for `protopy`/`protopyc`, `_rpath_lib` for `libprotoPython`).
- **protoPython `STDLIB_PATH` / `STDLIB_BUILD_PATH` (`src/runtime/CMakeLists.txt:10-21`) and `resolveStdLibPath` (`src/runtime/main.cpp:125-140`)** — *no change*; this is the design the other repositories are measured against. Its `getExecutablePath()` (`:38-54`) is the source Task 7 ports from.
- **protoPython `if(DEFINED PROTO_CORE_PREFIX)` in `src/compiler/CMakeLists.txt:16`** — **gap found while writing and fixed inline**: the survey listed the file but not this condition, which Step 2 of Task 2 would silently break (an installed package found in a default prefix leaves `PROTO_CORE_PREFIX` undefined). Task 2 Step 3 replaces it with `PROTOPYTHON_PROTOCORE_EXTERNAL`.
- **protoPython's unqualified `protoCore` link name and `$<TARGET_FILE_DIR:protoCore>`** — **gap found while writing**: the survey described protoPython's `add_subdirectory` mode but not that four files link the bare target name. Task 2 Step 2 promotes and aliases the imported target so all five references are untouched.
- **protoJS `install(TARGETS protojs ...)` (`:229-232`)** — *no change*; the comment at `:277-283` correctly states there is nothing else to install.
- **protoJS install RPATH (`:218-226`)** — *no change*; verified in Task 8.
- **protoJS test-only `simple_addon` / `fixture_addon`** — *no change*; not installed, correctly.
- **protoJS CPack block (`:277-314`)** — generators guarded (Task 6 Step 3), dependency bounded, package names pinned. The survey framed D-I2 as three repositories; protoJS is the fourth with the same defect, and it is fixed.
- **protoJS `packaging/build_deb.sh`, the three Linux templates, the macOS template, the WiX template, `PROCEDURES.md`, `DOCUMENTATION.md`** — all six version checks raised (Task 6 Steps 4-8); the script's build directory and staging location corrected (Step 9). `PROCEDURES.md` and `DOCUMENTATION.md` are *not* edited by this plan — **gap noted**: they describe the `1.0.0` procedure. Task 6 Step 11 updates `docs/INSTALLATION.md`; the executor should grep both packaging documents for `1.0.0` and `build/protojs` and correct them in the same commit if they mention either.
- **protoJS's committed `protoJS_0.1.0_amd64.deb` and `protoJS_staging/`** — *no change*; recorded, and Task 6 Step 9 makes the script stop writing to those paths.
- **protoST `install(TARGETS protost ...)`, `install(DIRECTORY lib/ ...)`, LICENSE, `docs/`, the optional `editor-integration/vscode`** (`:181-208`) — *no change*; the `.st` payload is verified in Tasks 3, 7 and 8.
- **protoST install RPATH (`:167-178`)** — *no change*.
- **protoST CPack (`:210-244`)** — generators guarded, dependency bounded, names pinned, the two "(unverified on a Linux build host)" comments kept and extended to name D-I6.
- **protoST `discoverStdlibDir()` (`:1946-1999`)** — rewritten in Task 7 (D-I5).
- **protoST's `libreadline`, `nlohmann_json` and Catch2 dependencies** — *no change*. `libreadline` is a real runtime dependency of `protost` and is **not** declared in the DEB. **Gap noted**: only protoScala enables `CPACK_DEBIAN_PACKAGE_SHLIBDEPS` (Task 5 Step 4). If the maintainer wants the same for protoST and protoClojure, it is the same one line; this plan does not add it to them because it changes their existing package metadata and D-I2's scope was the generator guard.
- **protoClojure `install(TARGETS protoclj ...)`, LICENSE, README, `docs/`, `examples/`, `benchmarks/`** (`:145-177`) — *no change*; the survey's finding that none of it is read at runtime was re-checked (`grep -rn "prelude\|PROTOCLJ_LIB\|discoverStdlib" protoClojure/src/` finds nothing) and is recorded in the new `docs/INSTALLATION.md`.
- **protoClojure install RPATH (`:134-142`)** and **CPack (`:179-215`)** — RPATH unchanged; CPack guarded and bounded in Task 4.
- **protoScala `install(TARGETS protoscala ...)` and `install(FILES LICENSE README.md ...)`** (`:188-193`) — *no change*; they become the payload of the repository's first package.
- **protoScala install RPATH (`:175-186`)** — kept; only the sentence in its comment that defers CPack is removed (Task 5 Step 4).
- **protoScala embedded prelude (`:64-73`)** — *no change*, and recorded as the reason protoScala has no data-file risk.
- **protoScala `ProtoMPSCQueue` feature probe (`:132-146`)** — kept, with the header path now reported (Task 5 Step 3) and listed as risk R7.
- **The survey's claim that no repository sets `CPACK_DEBIAN_PACKAGE_NAME` or `CPACK_RPM_PACKAGE_NAME`** — confirmed by re-grepping all six files, and reversed by this plan: every repository now pins both, with the observable names unchanged.
- **The survey's cross-repo table** — every row's `install()`, CPack, protoCore lookup and runtime-data columns is addressed by a task above. The only column with no task is "Own shared lib" for protoPython (`libprotoPython`, `SOVERSION 0`), which is recorded in "Recorded, not asked" and left alone.
- **Naming consistency check across tasks.** `PROTOCORE_MIN_VERSION`, `PROTOCORE_MIN_VERSION_FULL`, `PROTOCORE_NEXT_MAJOR`, `PROTOCORE_ABI_SOVERSION`, `PROTOCORE_REQUIRE_PACKAGE`, `PROTOCORE_LIBRARY`, `PROTOCORE_INCLUDE_DIRS`, `PROTOCORE_DIR`, `protoCore::protoCore`, `protoCore_VERSION`, `protoCore_SOVERSION`, `protoCore_INCLUDE_DIR`, `protoCore_LIB_DIR`, `PROTOPYTHON_PROTOCORE_EXTERNAL`, `PROTOPYTHON_PROTOCORE_INCLUDE_DIR` are each used with one spelling and one meaning in every task, as tabulated in the "Interface contract" section. protoJS keeps its own pre-existing `PROTOCORE_LIBRARY_DIR` (as opposed to protoST's, protoClojure's and protoScala's `PROTOCORE_LIB_DIR`) because that is what its file already calls it at `:48`; both are local to their own file and neither crosses a task boundary.
- **Gap found and fixed while writing:** the first draft of the runtime discovery block used a generator expression inside `message(FATAL_ERROR)` to name the prefix. Generator expressions are not evaluated by `message()`. Every task now builds a `_protoCore_where` string with a plain `if` first.
- **Gap found and fixed while writing:** the first draft asserted the sibling protoCore's version by reading `protoCore_VERSION` after `add_subdirectory()`. `project()` sets that variable in the subdirectory's scope, not the parent's. Task 2 reads the `SOVERSION` target property instead, which is readable from the parent.

---

## Implementation status (2026-09-23)

Implemented on the branch `feature/phase-i-installers` in all six repositories.
Nothing was pushed. Where reality differed from the plan, reality won; the
differences are listed below.

### Verified on Linux

| Item | Result |
|---|---|
| `protoCoreConfig.cmake`, `protoCoreConfigVersion.cmake`, `protoCoreTargets*.cmake`, `protoCore.pc` | Emitted and installed; `pkg-config --variable=soversion protoCore` prints `2` |
| Version gate | `find_package(protoCore 2.0)` and `2.1` accept 2.1.0; `2.2` and `3.0` are refused; an ABI-file assertion fires when `libprotoCore.so.2` is missing from a prefix that still has the CMake files |
| Six `.deb` packages | `protocore` 2.1.0, `protopython` 1.0.0, `protost` 0.3.0, `protoclojure` 0.0.1, `protoscala` 0.3.0, `protojs` 0.1.0, plus the hand-built `protoJS` 0.1.0 |
| Dependencies | Five bounded relations; protoScala carries `>= 2.1.0`, the other four `>= 2.0.0`, all with `<< 3.0.0` |
| `ldd` | All six binaries resolve `libprotoCore.so.2` inside the stage prefix, and again inside the extracted package root; none resolves to `/usr/local` |
| Smoke tests | `protopy` imports `json` from the installed standard library; `protojs`, `protost` (importing `stream` from `share/protoST/lib`), `protoclj` and `protoscala` each run a program — from the stage prefix *and* from the extracted `.deb` payload |
| D-I7 | A protoScala configure with `-DPROTO_CORE_PREFIX=/usr/local` now fails with a `FATAL_ERROR` naming the required version. The same discovery code from `main`, run against the same prefix, accepts `/usr/local/lib/libprotoCore.so.1.0.0` |
| Suites | protoCore 438/438, protoPython 582/583 (pre-existing `protopy_import_site`), protoJS ctest 34/34 and test262 `built-ins/Object+Reflect+Proxy` 3619 passed, protoST 833/833, protoClojure 383/383, protoScala 694/694 |

### Not verified

Every macOS and every Windows artefact, and every RPM: no such host, and
`rpmbuild` is not installed. The two "What a human must do" sections above list
what remains, unchanged.

### Where the plan was wrong, and what was done instead

1. **protoCore is 2.1.0, not 2.0.0.** protoScala's floor is therefore **2.1**,
   not the 2.0 the plan assumed for all five runtimes: its actor mailbox needs
   `ProtoMPSCQueue`, which arrived in 2.1.0. The other four were checked for a
   similar floor and have none — no other runtime references any 2.1 API — so
   they stay at 2.0. protoCore's version is no longer left alone; the packaging
   machinery is additive and the CHANGELOG entry went under the existing
   `## [Unreleased]`.
2. **R7 was fixed, not merely documented.** protoScala's `ProtoMPSCQueue`
   substring probe is replaced by a version comparison, so the mailbox backend
   cannot depend on which discovery mode was used.
3. **protoCore's install block is inside `if(CMAKE_PROJECT_NAME STREQUAL
   "protoCore")`**, which the plan's line numbers predate. The new
   `install(EXPORT)` and package-config rules went inside that guard.
4. **`set(protoCore_NOT_FOUND_MESSAGE a b c)` builds a list**, and CMake printed
   it with `;` separators. Changed to `string(CONCAT ...)`.
5. **protoPython's `install(TARGETS ...)` had one trailing `COMPONENT`**, which
   in that signature binds to the `ARCHIVE` group alone, so `protopy` and
   `libprotoPython` were in the default `Unspecified` component and no package
   ever contained them. Fixed per artifact group. Pre-existing, and invisible
   until a package was actually inspected.
6. **protoPython's tests could not build in package mode**: `gtest_main` comes
   from the `FetchContent` in protoCore's `test/` directory, which is only
   processed when protoCore is a subdirectory. CPack builds `all` first, so
   `cpack` was impossible. The GoogleTest-based tests are now guarded on
   `TARGET gtest_main`.
7. **protoJS included `"headers/protoCore.h"` in 89 files** — protoCore's source
   layout, which no installed prefix has. It could not compile against an
   installed protoCore at all. Normalised to `"protoCore.h"`, which resolves in
   both discovery modes and matches the other three runtimes.
8. **protoST's stdlib candidate order was wrong on Linux too.** `<exe>/../lib`
   was probed before `<exe>/../share/protoST/lib`, and in an installation the
   former is `<prefix>/lib` — the library directory. An installed `protost`
   failed with "module not found: stream". The installed layouts are now probed
   first. The plan's Task 7 Step 6 would have failed as written.
9. **`CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON` was dropped** (Task 5 Step 4; risk R9
   materialised). `dpkg-shlibdeps` resolves every `NEEDED` entry to the
   distribution package that owns it, and no distribution owns
   `libprotoCore.so.2`: it fails and takes the whole `.deb` down.
10. **The Task 6 done-when grep is a false positive as written.** No `1.0.0`
    protoCore reference survives under `packaging/templates/`, but the WiX
    template's own product version `0.1.0.0` contains the substring `1.0.0`.
11. **`packaging/PROCEDURES.md` and `packaging/DOCUMENTATION.md` were updated**
    in the protoJS commit, as the plan's self-review said the executor should.
12. **GitNexus impact analysis was not run** for protoJS: the `gitnexus_*` tools
    are not available in this session. No symbol was edited — the source change
    is one `#include` directive per file — and the full ctest and test262 subset
    were re-run instead.
