// EXPECT: 17 0 0 0
// Scala's Source is CONSUMED as it is read, and so is protoScala's: scalac 3.9.0
// answers 17 and then nothing for `mkString` followed by `getLines()`, because
// the underlying reader is exhausted (D101). protoScala streams a file rather
// than loading it whole, so this is the same behaviour for the same reason.
val src = Source.fromFile("_data/three.txt")
println(src.mkString.length + " " + src.getLines().length + " " +
        src.mkString.length + " " + src.getLines().length)
