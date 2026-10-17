#!/usr/bin/env bash
#
# CLI check: a script whose first line is `#!/usr/bin/env protoscala` runs as a
# command. The line is skipped (Scala 3's rule: only when `#!` are the first two
# characters of the file), errors keep the file's real line numbers, and the
# shipped filters (examples/column.scala, examples/sum.scala) chain in pipelines
# with their exit statuses intact.
#
# Usage: shebang.sh <path-to-protoscala> <tests-dir>
set -u
P="${1:?usage: shebang.sh <protoscala> <tests-dir>}"
T="${2:?usage: shebang.sh <protoscala> <tests-dir>}"
R="$(cd "$T/.." && pwd)"
# In the build tree, never /tmp.
work=$(mktemp -d -p "$PWD" shebang.XXXXXX) || { echo "FAIL: mktemp"; exit 1; }
trap 'rm -rf "$work"' EXIT
fail() { echo "FAIL: $*"; exit 1; }

# 1. Through protoscala: the line is skipped, and an error on line 3 says 3.
printf '#!/usr/bin/env protoscala\nprintln(3 + 4)\n' >"$work/ok.scala"
out=$(timeout 60s "$P" "$work/ok.scala" 2>&1); rc=$?
[[ $rc -eq 0 && "$out" == "7" ]] || fail "plain run: exit $rc, output '$out'"
printf '#!/usr/bin/env protoscala\nprintln(1)\nprintln(undefinedName)\n' >"$work/bad.scala"
out=$(timeout 60s "$P" "$work/bad.scala" 2>&1); rc=$?
[[ $rc -ne 0 && "$out" == *"bad.scala:3:9: error: Not found: undefinedName"* ]] || fail "error line: exit $rc, output '$out'"

# Interpreter lines are a kernel feature Windows does not have; there only the
# check above applies.
if command -v cygpath >/dev/null 2>&1; then echo "OK (no #! execution on Windows)"; exit 0; fi

# 2. As commands: `env` must find this build's protoscala first on PATH.
mkdir "$work/bin" && ln -s "$P" "$work/bin/protoscala" || fail "symlink"
export PATH="$work/bin:$PATH"
cp "$R/examples/column.scala" "$R/examples/sum.scala" "$work/" || fail "cp examples"
chmod +x "$work/column.scala" "$work/sum.scala"
cd "$work" || fail "cd"

out=$(printf 'pump-1 12\npump-2 30\n\nshort\n' | timeout 60s ./column.scala 2 | timeout 60s ./sum.scala 2>&1)
[[ "$out" == "42" ]] || fail "column | sum printed '$out'"
out=$(seq 1 100 | timeout 60s ./sum.scala 2>&1)
[[ "$out" == "5050" ]] || fail "seq | sum printed '$out'"

# 3. Exit statuses reach the shell: 1 for a bad line, 2 for a usage error.
out=$(printf '1\nx\n' | timeout 60s ./sum.scala 2>&1); rc=$?
[[ $rc -eq 1 && "$out" == *"line 2 is not a number: x"* ]] || fail "sum on bad input: exit $rc, '$out'"
out=$(timeout 60s ./column.scala </dev/null 2>&1); rc=$?
[[ $rc -eq 2 && "$out" == "usage: column.scala N"* ]] || fail "column usage: exit $rc, '$out'"

# 4. A filter stops when its reader goes away (head), instead of running on.
out=$(seq 1 200000 | sed 's/^/p /' | timeout 60s ./column.scala 2 | head -n 2 | tr '\n' ' ')
[[ "$out" == "1 2 " ]] || fail "| head printed '$out'"

echo OK
