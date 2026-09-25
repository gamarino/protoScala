// EXPECT: false true true false false
FileIO.delete("scratch.txt")
val before = FileIO.exists("scratch.txt")
FileIO.write("scratch.txt", "x")
val after = FileIO.exists("scratch.txt")
val removed = FileIO.delete("scratch.txt")
// `delete` answers `true` when it removed a file and `false` when there was
// nothing at the path. It never answers `false` for a failure -- see the next
// section.
val again = FileIO.delete("scratch.txt")
println(s"$before $after $removed $again ${FileIO.exists("scratch.txt")}")
