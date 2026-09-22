#!/usr/bin/env bash
#
# CLI check: REPL result names. Failed and Unit-valued inputs do not use up a
# resN name; String results and definitions are echoed with quotes, as the
# Scala 3 REPL does. Also: a UTF-8 byte-order mark at the start of a script
# or of a :load-ed file is accepted.
#
# Usage: repl-results.sh <path-to-protoscala>
set -u
P="${1:?usage: repl-results.sh <protoscala>}"
work=$(mktemp -d -p "$PWD" repl-results.XXXXXX)
trap 'rm -rf "$work"' EXIT
mkdir "$work/home"
printf '\xEF\xBB\xBFval fromBom = 7\nprintln("bom ok")\n' >"$work/bom.scala"
out=$(printf '%s\n' '1 / 0' 'println("side")' '41 + 1' '"hi"' 'val s = "x"' '()' \
          'nope' 'res0 + 1' "res1 + s" ":load $work/bom.scala" 'fromBom' \
      | HOME="$work/home" timeout 60s "$P" 2>"$work/err")
rc=$?
[[ $rc -eq 0 ]] || { echo "FAIL: exit $rc"; echo "$out"; exit 1; }
plain="${out//scala> /}"
for piece in 'val res0 = 42' 'val res1 = "hi"' 'val s = "x"' 'val res2 = 43' \
              'val res3 = "hix"' 'bom ok' 'val res4 = 7'; do
    grep -qxF -- "$piece" <<<"$plain" || { echo "FAIL: no '$piece' in:"; echo "$out"; exit 1; }
done
out=$(HOME="$work/home" timeout 60s "$P" "$work/bom.scala" 2>&1); rc=$?
[[ $rc -eq 0 && "$out" == "bom ok" ]] || { echo "FAIL (script with BOM): exit $rc, '$out'"; exit 1; }
echo OK
