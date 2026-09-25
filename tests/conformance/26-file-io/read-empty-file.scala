// EXPECT: 0 0
// An empty file has NO lines, not one empty line: scalac 3.9.0 answers List().
// The distinction matters -- a splitter that emitted a trailing empty element
// would answer 1 here and would also be wrong about every file that ends in a
// newline.
val src = Source.fromFile("_data/empty.txt")
println(src.getLines().length + " " + src.mkString.length)
