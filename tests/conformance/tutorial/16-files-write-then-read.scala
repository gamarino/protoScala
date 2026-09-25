// EXPECT: 3 lines, 18 characters
FileIO.write("notes.txt", "milk\nbread\napples\n")

val src = Source.fromFile("notes.txt")
val lines = src.getLines()
val size = src.mkString.length
src.close()

println(s"${lines.length} lines, $size characters")
