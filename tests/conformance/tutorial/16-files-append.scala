// EXPECT: first|second|third
FileIO.delete("journal.txt")
FileIO.append("journal.txt", "first\n")
FileIO.append("journal.txt", "second\n")
FileIO.append("journal.txt", "third\n")
println(Source.fromFile("journal.txt").getLines().mkString("|"))
