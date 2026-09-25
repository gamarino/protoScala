// EXPECT: false
// Writes go under PROTOSCALA_TEST_TMP, a directory inside the BUILD tree that
// tests/CMakeLists.txt creates for this fixture. Nothing here touches a path the
// fixture did not create, and the fixture refuses to run at all rather than fall
// back to the working directory -- which would be the source tree.
val tmp = System.getenv("PROTOSCALA_TEST_TMP")
if tmp == "" then throw new IllegalStateException("PROTOSCALA_TEST_TMP is not set")
// "There was nothing there" is an answer, not a failure: `delete` reports it as
// `false`, exactly as java.io.File.delete() does. Every OTHER failure raises --
// see delete-refuses-a-directory.scala.
println(FileIO.delete(tmp + "/never-created-this-one.txt"))
