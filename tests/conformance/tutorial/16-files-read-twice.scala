// EXPECT: 3 3
FileIO.write("twice.txt", "a\nb\nc\n")
val src = Source.fromFile("twice.txt")
// A protoScala source may be read again. On the JVM the second answer would be
// an empty list, because Scala's Source is consumed as it is read (D101).
println(s"${src.getLines().length} ${src.getLines().length}")
