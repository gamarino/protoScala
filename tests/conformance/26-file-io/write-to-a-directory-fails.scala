// EXPECT: FileNotFoundException Is a directory
// Writes go under PROTOSCALA_TEST_TMP, a directory inside the BUILD tree that
// tests/CMakeLists.txt creates for this fixture. Nothing here touches a path the
// fixture did not create, and the fixture refuses to run at all rather than fall
// back to the working directory -- which would be the source tree.
val tmp = System.getenv("PROTOSCALA_TEST_TMP")
if tmp == "" then throw new IllegalStateException("PROTOSCALA_TEST_TMP is not set")
// Writing where a directory already is. The message must name the path and say
// what went wrong; an implementation that dropped the errno would report
// "something failed" and leave the programmer guessing.
try
  FileIO.write(tmp, "x")
  println("FAIL: a directory was overwritten")
catch case e: IOException =>
  println(e.getClass + " " + (if e.getMessage.contains("(Is a directory)") then "Is a directory" else e.getMessage))
