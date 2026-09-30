// EXPECT: 3 0
FileIO.write("twice.txt", "a\nb\nc\n")
val src = Source.fromFile("twice.txt")
// A source is consumed as it is read, as Scala's is: the first pass reads every
// line, and the second finds none left.
println(s"${src.getLines().length} ${src.getLines().length}")
