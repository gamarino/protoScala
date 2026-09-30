// EXPECT: 3 lines, 18 characters
FileIO.write("notes.txt", "milk\nbread\napples\n")

val src = Source.fromFile("notes.txt")
val lines = src.getLines().toList
src.close()
val size = Source.fromFile("notes.txt").mkString.length

println(s"${lines.length} lines, $size characters")
