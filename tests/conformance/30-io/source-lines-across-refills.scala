// EXPECT: 20000 200010000 true | 4097 true true | 2 65535 c | 2 100000 after | a|b||c|last
// getLines() takes a line its source already holds without leaving the
// collector's quorum, and reads only when the buffer lacks one. Sources read
// 64 KiB at a time, so these files put lines across many refills, and put a
// CR on the last byte of the first read: before a LF (one CRLF, not a lone CR
// and an empty line) and before another character (a lone CR). Mixed
// terminators ("\n", "\r\n", a lone "\r") and a last line without one are
// split as Scala splits them (D100).
val tmp = System.getenv("PROTOSCALA_TEST_TMP")
if tmp == "" then throw new IllegalStateException("PROTOSCALA_TEST_TMP is not set")
def readAll(name: String, text: String): List[String] =
  val path = tmp + "/" + name
  FileIO.write(path, text)
  val src = Source.fromFile(path)
  val lines = src.getLines().toList
  src.close()
  lines

// 20,000 numbered lines, the terminator cycling through LF, CRLF and CR.
val pad = "p" * 40
val terms = List("\n", "\r\n", "\r")
val text = (1 to 20000).map(i => "line-" + i + "-" + pad + terms((i - 1) % 3)).mkString
val many = readAll("refills-many.txt", text)
var sum = 0L
var clean = true
var k = 1
for line <- many do
  val n = line.substring(5, line.indexOf("-", 5)).toInt
  sum += n
  if n != k || line != "line-" + n + "-" + pad then clean = false
  k += 1

// 4,095 lines of 16 bytes fill 65,520 bytes; the next line's CR is byte
// 65,535, the last of the first read, and its LF the first of the second.
val row = "r" * 15 + "\n"
val crlfAtEdge = readAll("refills-crlf-edge.txt", row * 4095 + "c" * 15 + "\r\n" + "end\n")
val edgeOk = crlfAtEdge.length == 4097 && crlfAtEdge(4095) == "c" * 15 && crlfAtEdge(4096) == "end"
val rowsOk = crlfAtEdge.take(4095).forall(_ == "r" * 15)
// A lone CR on the same byte.
val loneCr = readAll("refills-cr-edge.txt", "b" * 65535 + "\r" + "c")
// A line longer than one read, then a last line without a terminator.
val long = readAll("refills-long.txt", "q" * 100000 + "\n" + "after")
val mixed = readAll("refills-mixed.txt", "a\r\nb\n\rc\rlast")
println(many.length.toString + " " + sum + " " + clean + " | " +
        crlfAtEdge.length + " " + edgeOk + " " + rowsOk + " | " +
        loneCr.length + " " + loneCr(0).length + " " + loneCr(1) + " | " +
        long.length + " " + long(0).length + " " + long(1) + " | " + mixed.mkString("|"))
