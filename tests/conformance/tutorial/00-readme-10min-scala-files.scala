// EXPECT: one | two
// Point 4 of "protoScala in 10 minutes -- for Scala programmers": where the
// absence of Java is felt is writing, not reading.
FileIO.write("notes.txt", "one\ntwo\n")
val lines = Source.fromFile("notes.txt").getLines()   // a List[String], not an Iterator
println(lines.mkString(" | "))                        // one | two
