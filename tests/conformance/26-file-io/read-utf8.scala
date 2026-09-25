// EXPECT: 8 héllo €
// The file is decoded as UTF-8, so its length is counted in characters and not
// in bytes: "héllo €" is 7 characters plus the newline, in 11 bytes. scalac
// 3.9.0 answers 8 for `mkString.length` too.
val src = Source.fromFile("_data/utf8.txt")
println(src.mkString.length + " " + src.getLines()(0))
