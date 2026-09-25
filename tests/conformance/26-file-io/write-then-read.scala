// EXPECT: true 2 one|two
// Writes go under PROTOSCALA_TEST_TMP, a directory inside the BUILD tree that
// tests/CMakeLists.txt creates for this fixture. Nothing here touches a path the
// fixture did not create, and the fixture refuses to run at all rather than fall
// back to the working directory -- which would be the source tree.
val tmp = System.getenv("PROTOSCALA_TEST_TMP")
if tmp == "" then throw new IllegalStateException("PROTOSCALA_TEST_TMP is not set")
val path = tmp + "/write-then-read.txt"
FileIO.write(path, "one\ntwo\n")
val lines = Source.fromFile(path).getLines()
println(FileIO.exists(path) + " " + lines.length + " " + lines.mkString("|"))
FileIO.delete(path)
