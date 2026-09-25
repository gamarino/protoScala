// EXPECT: one|two|three
// Writes go under PROTOSCALA_TEST_TMP, a directory inside the BUILD tree that
// tests/CMakeLists.txt creates for this fixture. Nothing here touches a path the
// fixture did not create, and the fixture refuses to run at all rather than fall
// back to the working directory -- which would be the source tree.
val tmp = System.getenv("PROTOSCALA_TEST_TMP")
if tmp == "" then throw new IllegalStateException("PROTOSCALA_TEST_TMP is not set")
val path = tmp + "/append.txt"
// `append` on a path with nothing at it creates the file, like Scala's
// FileWriter(path, true) and like a shell's `>>`.
FileIO.delete(path)
FileIO.append(path, "one\n")
FileIO.append(path, "two\n")
FileIO.append(path, "three\n")
println(Source.fromFile(path).getLines().mkString("|"))
FileIO.delete(path)
