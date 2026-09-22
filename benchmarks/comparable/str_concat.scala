// EXPECT: 2000
// str_concat.scala - N = 2000 string concatenations, `s = s + "x"`.
// Twin of protoPython's benchmarks/str_concat_loop.py and protoST's
// comparable/str_concat.st. Prints the final string length.
// Result: 2000.

def concat(n: Int): String =
  var s = ""
  var i = 0
  while i < n do
    s = s + "x"
    i += 1
  s

@main def benchStrConcat(): Unit =
  println(concat(2000).length)
