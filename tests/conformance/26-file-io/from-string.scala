// EXPECT: 2 x|y
// `Source.fromString` reads text that is already in memory through exactly the
// same surface, and through exactly the same line splitter -- which is why there
// is one splitter and not two.
val lines = Source.fromString("x\ny").getLines()
println(lines.length + " " + lines.mkString("|"))
