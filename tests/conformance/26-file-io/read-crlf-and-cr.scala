// EXPECT: a|b a|b 6 4
// Scala's `getLines()` breaks on "\n", "\r\n" AND a lone "\r", and strips the
// whole terminator. Verified against scalac 3.9.0: both files answer
// List(a, b). `mkString` keeps the bytes, which is how this fixture proves the
// carriage returns really are in the files and were not normalised away by git
// (see _data/.gitattributes). A source is consumed as it is read, so the lengths
// come from sources of their own.
def src(name: String) = Source.fromFile("_data/" + name)
println(src("crlf.txt").getLines().mkString("|") + " " + src("cr.txt").getLines().mkString("|") +
        " " + src("crlf.txt").mkString.length + " " + src("cr.txt").mkString.length)
