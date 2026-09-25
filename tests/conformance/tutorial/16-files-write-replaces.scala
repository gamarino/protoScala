// EXPECT: after
FileIO.write("draft.txt", "before, and rather longer\n")
FileIO.write("draft.txt", "after\n")
// `write` replaces the whole file. There is no "write at position" and no
// half-replaced file: use `append` when you mean to add.
println(Source.fromFile("draft.txt").mkString.trim)
