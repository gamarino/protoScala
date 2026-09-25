// EXPECT: false true true false
// Writes go under PROTOSCALA_TEST_TMP, a directory inside the BUILD tree that
// tests/CMakeLists.txt creates for this fixture. Nothing here touches a path the
// fixture did not create, and the fixture refuses to run at all rather than fall
// back to the working directory -- which would be the source tree.
val tmp = System.getenv("PROTOSCALA_TEST_TMP")
if tmp == "" then throw new IllegalStateException("PROTOSCALA_TEST_TMP is not set")
val path = tmp + "/exists-and-delete.txt"
FileIO.delete(path)
val before = FileIO.exists(path)
FileIO.write(path, "x")
val during = FileIO.exists(path)
val removed = FileIO.delete(path)
println(before + " " + during + " " + removed + " " + FileIO.exists(path))
