// EXPECT: 3 [a][][b]
// A blank line in the middle is a line. scalac 3.9.0 answers List(a, "", b).
val lines = Source.fromFile("_data/blank-lines.txt").getLines()
println(lines.length + " " + lines.map(l => "[" + l + "]").mkString)
