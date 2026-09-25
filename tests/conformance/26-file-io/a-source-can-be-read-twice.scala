// EXPECT: 17 3 17 3
// D101, a deliberate divergence. Scala's Source is CONSUMED as it is read:
// scalac 3.9.0 answers (17, List()) for `mkString` followed by `getLines()`,
// because the underlying iterator is exhausted. protoScala reads the file once,
// when it is opened, and answers the same thing however often it is asked.
val src = Source.fromFile("_data/three.txt")
println(src.mkString.length + " " + src.getLines().length + " " +
        src.mkString.length + " " + src.getLines().length)
