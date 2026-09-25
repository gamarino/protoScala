// EXPECT: 17 alpha
// `mkString` is the whole file, terminators and all: 17 bytes for
// "alpha\nbeta\ngamma\n", which is what scalac 3.9.0 answers.
val text = Source.fromFile("_data/three.txt").mkString
println(text.length + " " + text.split("\n")(0))
