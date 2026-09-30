#!/usr/bin/env bash
#
# CLI check: standard input. StdIn.readLine() answers each line and null at the
# end of input; readInt / readDouble parse a line; Source.stdin.getLines()
# iterates what is left; a prompt written with print() is flushed before the
# program waits for input.
#
# Usage: io-stdin.sh <path-to-protoscala>
set -u
P="${1:?usage: io-stdin.sh <protoscala>}"
work=$(mktemp -d -p "$PWD" io-stdin.XXXXXX)
trap 'rm -rf "$work"' EXIT
fail() { echo "FAIL: $*"; exit 1; }

# 1. readLine until null, with a non-ASCII line and a CRLF line.
cat >"$work/lines.scala" <<'SCALA'
var n = 0
var line = StdIn.readLine()
while line != null do
  n += 1
  println(n.toString + ":" + line + ":" + line.length)
  line = StdIn.readLine()
println("end")
SCALA
out=$(printf 'alpha\nñandú\r\nlast' | "$P" "$work/lines.scala" 2>&1); rc=$?
expected=$'1:alpha:5\n2:ñandú:5\n3:last:4\nend'
[[ $rc -eq 0 && "$out" == "$expected" ]] || fail "readLine: exit $rc, output '$out'"

# 2. readInt and readDouble, then the rest through Source.stdin.
cat >"$work/numbers.scala" <<'SCALA'
val a = StdIn.readInt()
val b = StdIn.readDouble()
val rest = Source.stdin.getLines().toList
println((a * 2).toString + " " + (b * 2) + " " + rest)
SCALA
out=$(printf '21\n1.25\nx\ny\n' | "$P" "$work/numbers.scala" 2>&1); rc=$?
[[ $rc -eq 0 && "$out" == "42 2.5 List(x, y)" ]] || fail "readInt/readDouble: exit $rc, output '$out'"

# 3. readInt on a line that is not a number is a NumberFormatException, and at
#    the end of input an EOFException, as Scala's StdIn raises them.
cat >"$work/bad.scala" <<'SCALA'
val first =
  try StdIn.readInt().toString
  catch case e: NumberFormatException => e.getClass
val second =
  try StdIn.readInt().toString
  catch case e: EOFException => e.getClass
println(first + " " + second)
SCALA
out=$(printf 'twelve\n' | "$P" "$work/bad.scala" 2>&1); rc=$?
[[ $rc -eq 0 && "$out" == "NumberFormatException EOFException" ]] || fail "readInt errors: exit $rc, output '$out'"

# 4. A prompt without a newline is visible before the program blocks on input:
#    the output of `print` reaches the pipe before the answer is read.
cat >"$work/prompt.scala" <<'SCALA'
print("name? ")
val name = StdIn.readLine()
println("hello " + name)
SCALA
out=$(printf 'Ada\n' | "$P" "$work/prompt.scala" 2>&1); rc=$?
[[ $rc -eq 0 && "$out" == "name? hello Ada" ]] || fail "prompt: exit $rc, output '$out'"

# 5. Source.stdin streams: a line is processed before the next one is written.
#    The writer waits for the program's answer to line 1 before sending line 2,
#    which deadlocks (and times out) if the program read all input first.
cat >"$work/stream.scala" <<'SCALA'
for line <- Source.stdin.getLines() do
  println("got " + line)
SCALA
fifo_in="$work/in.fifo"; fifo_out="$work/out.fifo"
mkfifo "$fifo_in" "$fifo_out"
( timeout 30s "$P" "$work/stream.scala" <"$fifo_in" >"$fifo_out" 2>&1 ) &
runner=$!
exec 7>"$fifo_in" 8<"$fifo_out"
echo "one" >&7
IFS= read -r -t 20 first <&8 || first="(timeout)"
echo "two" >&7
exec 7>&-
IFS= read -r -t 20 second <&8 || second="(timeout)"
exec 8<&-
wait "$runner"; rc=$?
[[ $rc -eq 0 && "$first" == "got one" && "$second" == "got two" ]] ||
    fail "streaming stdin: exit $rc, got '$first' then '$second'"

echo OK
