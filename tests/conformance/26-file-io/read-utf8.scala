// EXPECT: 8 héllo €
// The file is decoded as UTF-8, so its length is counted in characters and not
// in bytes: "héllo €" is 7 characters plus the newline, in 11 bytes. scalac
// 3.9.0 answers 8 for `mkString.length` too. A source is consumed as it is read,
// so each question gets a source of its own.
println(Source.fromFile("_data/utf8.txt").mkString.length + " " +
        Source.fromFile("_data/utf8.txt").getLines().next())
