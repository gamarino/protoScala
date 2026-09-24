#!/usr/bin/env bash
#
# CLI check: `import` at the REPL prompt (Phase 6).
#
# Three things are pinned here that a conformance fixture cannot reach, because
# a fixture is one file and one process:
#   - a module imported at the prompt is usable by later lines;
#   - an imported TYPE is usable by later lines, which is the whole reason
#     imports resolve at compile time;
#   - a FAILED import leaves the session usable, because Session::evaluate
#     compiles against a trial table and a failed line defines nothing.
#
# Usage: modules.sh <path-to-protoscala>
set -u
P="${1:?usage: modules.sh <protoscala>}"
work=$(mktemp -d -p "$PWD" cli-modules.XXXXXX)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/util"
cat >"$work/util/Strings.scala" <<'MODULE'
def shout(s: String): String = s.toUpperCase + "!"
case class Tag(name: String, weight: Int)
MODULE

out=$(printf '%s\n' \
        'import util.Strings' \
        'Strings.shout("hi")' \
        'import util.Strings.{Tag}' \
        'Tag("x", 2)' \
        'import util.Nope' \
        '1 + 1' \
      | PROTOSCALA_PATH="$work" timeout 60s "$P" 2>&1)
rc=$?
[[ $rc -eq 0 ]] || { echo "FAIL: exit $rc"; echo "$out"; exit 1; }

for piece in 'val res0 = "HI!"' 'val res1 = Tag(x,2)' \
             "no module found for 'util.Nope'" 'val res2 = 2'; do
    grep -qF -- "$piece" <<<"$out" || { echo "FAIL: no '$piece' in:"; echo "$out"; exit 1; }
done
echo OK
