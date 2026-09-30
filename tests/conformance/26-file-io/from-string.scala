// EXPECT: 2 x|y
// `Source.fromString` reads text that is already in memory through exactly the
// same surface, and through exactly the same line splitter -- which is why there
// is one splitter and not two. getLines() answers an Iterator, consumed as it is
// read, so it is turned into a List to be read twice.
val lines = Source.fromString("x\ny").getLines().toList
println(lines.length + " " + lines.mkString("|"))
