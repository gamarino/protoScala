// EXPECT: second
// Writes go under PROTOSCALA_TEST_TMP, a directory inside the BUILD tree that
// tests/CMakeLists.txt creates for this fixture. Nothing here touches a path the
// fixture did not create, and the fixture refuses to run at all rather than fall
// back to the working directory -- which would be the source tree.
val tmp = System.getenv("PROTOSCALA_TEST_TMP")
if tmp == "" then throw new IllegalStateException("PROTOSCALA_TEST_TMP is not set")
val path = tmp + "/write-replaces.txt"
FileIO.write(path, "first-and-much-longer\n")
FileIO.write(path, "second\n")
// A write that truncated nothing would leave the tail of the first text behind,
// so reading back the WHOLE file is what pins the replacement.
println(Source.fromFile(path).mkString.trim)
FileIO.delete(path)
