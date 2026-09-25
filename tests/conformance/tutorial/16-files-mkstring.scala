// EXPECT: 19 4
FileIO.write("poem.txt", "one\ntwo\nthree\nfour\n")

val whole = Source.fromFile("poem.txt").mkString
// `mkString` is every byte of the file, newlines included; `getLines()` is the
// same text with the newlines removed and the pieces handed to you separately.
println(s"${whole.length} ${Source.fromFile("poem.txt").getLines().length}")
