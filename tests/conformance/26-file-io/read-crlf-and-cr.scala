// EXPECT: a|b a|b 6 4
// Scala's `getLines()` breaks on "\n", "\r\n" AND a lone "\r", and strips the
// whole terminator. Verified against scalac 3.9.0: both files answer
// List(a, b). `mkString` keeps the bytes, which is how this fixture proves the
// carriage returns really are in the files and were not normalised away by git
// (see _data/.gitattributes).
val crlf = Source.fromFile("_data/crlf.txt")
val cr = Source.fromFile("_data/cr.txt")
println(crlf.getLines().mkString("|") + " " + cr.getLines().mkString("|") +
        " " + crlf.mkString.length + " " + cr.mkString.length)
