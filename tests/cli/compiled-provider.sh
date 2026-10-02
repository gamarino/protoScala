#!/usr/bin/env bash
#
# CLI check: CompiledModuleProvider (Phase 7 Task 10 Steps 1-4).
#
# A compiled module is importable. What that means precisely, and the test says it
# rather than the documentation:
#
#   1. `import util.Strings` finds `util/Strings.so` on PROTOSCALA_MODULE_PATH and its
#      members are reachable.
#   2. A `.scala` beside the `.so` still WINS, so installing a compiled module cannot
#      change the meaning of an import that already resolved.
#   3. A `.so` that defines `proto_module_main` is REFUSED: a script is not a module
#      (D8), with the wording D91 uses on the source path.
#   4. A `.so` that is not a protoCore module at all is refused by naming the symbol
#      that is missing, not with a dlsym error nobody can act on.
#   5. A miss still reports the SOURCE paths it tried, because a miss means neither a
#      source nor a compiled module was found and the source list is the actionable one.
#   6. Two imports of the same module share one module object (P3: loaded once).
#
# Usage: compiled-provider.sh <protoscala> <tests-dir> <protoscalac> <scratch-dir>
set -u
PROTOSCALA="${1:?usage: compiled-provider.sh <protoscala> <tests> <protoscalac> <scratch>}"
TESTS_DIR="${2:?}"
PROTOSCALAC="${3:?}"
SCRATCH="${4:?}"
# shellcheck source=platform.sh
source "$(dirname "$0")/platform.sh"

fails=0
# The scratch directory the harness passes. This script owns it and clears it, so it
# checks the path first -- but it checks what makes a path SAFE, not where the author
# happens to work.
#
# It did the latter once, and that is why this comment is long: the guard was a glob on
# the developer's own workspace prefix, so on a clean machine the build tree did not
# match, the guard refused, and the script exited 1 in 0.00 s having done nothing. CI
# went red on the two tests that were the whole point of the work, and the failure looked
# like the capability was broken rather than the guard. A test must not encode the
# machine it was written on.
require_private_scratch() {
    local p="$1"
    case "$p" in
        /*) ;;
        [A-Za-z]:/*) ;;   # Windows: CTest names the build tree as C:/...
        *) echo "FAIL: the scratch path must be absolute, got '$p'"; exit 1 ;;
    esac
    case "$p" in
        */../*|*/..) echo "FAIL: the scratch path must not contain '..': '$p'"; exit 1 ;;
    esac
    # At least three components and a non-empty last one, so '/', '/home' and '/home/x'
    # can never be the target of the rm below whatever the harness passes.
    local depth
    depth=$(awk -F/ '{print NF - 1}' <<<"$p")
    if [[ "$depth" -lt 3 || -z "${p##*/}" ]]; then
        echo "FAIL: refusing to clear '$p': not plainly a private scratch directory"
        exit 1
    fi
    if [[ -L "$p" ]]; then
        echo "FAIL: the scratch path is a symlink: '$p'"
        exit 1
    fi
}
require_private_scratch "$SCRATCH"
rm -rf "$SCRATCH"
mkdir -p "$SCRATCH/src" "$SCRATCH/modules/util" "$SCRATCH/build" "$SCRATCH/run"

# --- the module, as source -------------------------------------------------------
cat > "$SCRATCH/src/Strings.scala" <<'SCALA'
def shout(s: String): String = s.toUpperCase + "!"
def twice(s: String): String = s + s
SCALA

if ! "$PROTOSCALAC" "$SCRATCH/src/Strings.scala" -o "$SCRATCH/build" --build-so \
        --module-name util.Strings >"$SCRATCH/build.out" 2>&1; then
    echo "FAIL: protoscalac could not build the module"
    sed 's/^/  /' "$SCRATCH/build.out"
    exit 1
fi
cp "$SCRATCH/build/module.$SO" "$SCRATCH/modules/util/Strings.$SO"

# --- 1. the import resolves to the .so ------------------------------------------
cat > "$SCRATCH/run/importer.scala" <<'SCALA'
import util.Strings
@main def run(): Unit =
  println(Strings.shout("hello"))
  println(Strings.twice("ab"))
SCALA
out=$(PROTOSCALA_MODULE_PATH="$SCRATCH/modules" "$PROTOSCALA" "$SCRATCH/run/importer.scala" 2>&1)
want=$'HELLO!\nabab'
if [[ "$out" != "$want" ]]; then
    echo "FAIL: importing the compiled module printed:"; sed 's/^/  /' <<<"$out"
    fails=$((fails + 1))
fi

# A RELATIVE PROTOSCALA_MODULE_PATH is resolved against the working directory, as
# any relative path is. Windows refused to load a library named relatively, so this
# import failed there although the file was found.
out=$(cd "$SCRATCH" && PROTOSCALA_MODULE_PATH="modules" "$PROTOSCALA" "$SCRATCH/run/importer.scala" 2>&1)
if [[ "$out" != "$want" ]]; then
    echo "FAIL: importing through a relative PROTOSCALA_MODULE_PATH printed:"
    sed 's/^/  /' <<<"$out"
    fails=$((fails + 1))
fi

# The same program with no PROTOSCALA_MODULE_PATH must MISS, so the pass above is
# the provider's doing and not a source file that happened to be found.
out=$("$PROTOSCALA" "$SCRATCH/run/importer.scala" 2>&1)
grep -q "no module found for 'util.Strings'" <<<"$out" || {
    echo "FAIL: without PROTOSCALA_MODULE_PATH the import did not miss:"
    sed 's/^/  /' <<<"$out"; fails=$((fails + 1)); }

# --- 2. a .scala beside the .so wins --------------------------------------------
mkdir -p "$SCRATCH/run/util"
cat > "$SCRATCH/run/util/Strings.scala" <<'SCALA'
def shout(s: String): String = "from source"
def twice(s: String): String = "from source"
SCALA
out=$(PROTOSCALA_MODULE_PATH="$SCRATCH/modules" "$PROTOSCALA" "$SCRATCH/run/importer.scala" 2>&1)
want=$'from source\nfrom source'
if [[ "$out" != "$want" ]]; then
    echo "FAIL: the source module did not win over the compiled one:"
    sed 's/^/  /' <<<"$out"; fails=$((fails + 1))
fi
rm -rf "$SCRATCH/run/util"

# --- 3. a script is not a module (D8) -------------------------------------------
mkdir -p "$SCRATCH/build2" "$SCRATCH/modules/bad"
cat > "$SCRATCH/src/Script.scala" <<'SCALA'
@main def run(): Unit = println("I am a program")
SCALA
if "$PROTOSCALAC" "$SCRATCH/src/Script.scala" -o "$SCRATCH/build2" --build-so \
        >"$SCRATCH/build2.out" 2>&1; then
    cp "$SCRATCH/build2/module.$SO" "$SCRATCH/modules/bad/Script.$SO"
    cat > "$SCRATCH/run/imports-a-script.scala" <<'SCALA'
import bad.Script
@main def run(): Unit = println(1)
SCALA
    out=$(PROTOSCALA_MODULE_PATH="$SCRATCH/modules" "$PROTOSCALA" \
            "$SCRATCH/run/imports-a-script.scala" 2>&1)
    grep -q 'a module may not define an @main method' <<<"$out" || {
        echo "FAIL: a compiled script was not refused with D91's wording:"
        sed 's/^/  /' <<<"$out"; fails=$((fails + 1)); }
else
    echo "FAIL: protoscalac could not build the script"
    sed 's/^/  /' "$SCRATCH/build2.out"; fails=$((fails + 1))
fi

# --- 4. a .so that is not a protoCore module ------------------------------------
mkdir -p "$SCRATCH/modules/nope"
echo "$PS_EXPORT int unrelated() { return 0; }" > "$SCRATCH/src/notamodule.cpp"
if build_plain_library "$SCRATCH/modules/nope/Thing.$SO" "$SCRATCH/src/notamodule.cpp" \
        2>/dev/null; then
    cat > "$SCRATCH/run/imports-nonmodule.scala" <<'SCALA'
import nope.Thing
@main def run(): Unit = println(1)
SCALA
    out=$(PROTOSCALA_MODULE_PATH="$SCRATCH/modules" "$PROTOSCALA" \
            "$SCRATCH/run/imports-nonmodule.scala" 2>&1)
    grep -q 'proto_module_init not found' <<<"$out" || {
        echo "FAIL: a non-module .so was not refused by naming the symbol:"
        sed 's/^/  /' <<<"$out"; fails=$((fails + 1)); }
else
    # NOT a skip: --build-so above already ran a C++ compiler successfully, so failing
    # here means something else. A test that skips quietly reports nothing.
    echo "FAIL: could not compile a two-line .so, although --build-so worked"
    fails=$((fails + 1))
fi

# --- 5. a miss names the source paths -------------------------------------------
cat > "$SCRATCH/run/missing.scala" <<'SCALA'
import util.NotThere
@main def run(): Unit = println(1)
SCALA
out=$(PROTOSCALA_MODULE_PATH="$SCRATCH/modules" "$PROTOSCALA" "$SCRATCH/run/missing.scala" 2>&1)
grep -q "no module found for 'util.NotThere'" <<<"$out" || {
    echo "FAIL: a miss did not report the logical path:"; sed 's/^/  /' <<<"$out"
    fails=$((fails + 1)); }

# --- 6. one module object, however many importers -------------------------------
# P3: a module is loaded once per process. Two imports of the same path must reach
# the same object, which is observable through a `var` in the module's top level.
cat > "$SCRATCH/src/Counter.scala" <<'SCALA'
var calls = 0
def bump(): Int = { calls = calls + 1; calls }
SCALA
mkdir -p "$SCRATCH/build3"
if "$PROTOSCALAC" "$SCRATCH/src/Counter.scala" -o "$SCRATCH/build3" --build-so \
        --module-name util.Counter >"$SCRATCH/build3.out" 2>&1; then
    cp "$SCRATCH/build3/module.$SO" "$SCRATCH/modules/util/Counter.$SO"
    cat > "$SCRATCH/run/twice.scala" <<'SCALA'
import util.Counter
import util.Counter as Again
@main def run(): Unit =
  println(Counter.bump())
  println(Again.bump())
SCALA
    out=$(PROTOSCALA_MODULE_PATH="$SCRATCH/modules" "$PROTOSCALA" "$SCRATCH/run/twice.scala" 2>&1)
    want=$'1\n2'
    if [[ "$out" != "$want" ]]; then
        echo "FAIL: two imports did not share one module object (got $(tr '\n' '|' <<<"$out")):"
        sed 's/^/  /' <<<"$out"; fails=$((fails + 1))
    fi
else
    echo "FAIL: protoscalac could not build the counter module"
    sed 's/^/  /' "$SCRATCH/build3.out"; fails=$((fails + 1))
fi

[[ $fails -eq 0 ]] || exit 1
echo OK
