// EXPECT: 3 gamma
// A last line with no terminator is still a line. scalac 3.9.0 answers
// List(alpha, beta, gamma) for this file, the same as for the one that ends in a
// newline -- which is why both files are in `_data` and both are read.
val lines = Source.fromFile("_data/no-trailing-newline.txt").getLines()
println(lines.length + " " + lines.last)
