// EXPECT: FileNotFoundException No such file or directory
// Writes go under PROTOSCALA_TEST_TMP, a directory inside the BUILD tree that
// tests/CMakeLists.txt creates for this fixture. Nothing here touches a path the
// fixture did not create, and the fixture refuses to run at all rather than fall
// back to the working directory -- which would be the source tree.
val tmp = System.getenv("PROTOSCALA_TEST_TMP")
if tmp == "" then throw new IllegalStateException("PROTOSCALA_TEST_TMP is not set")
// `write` creates a FILE, never the directories above it. The failure says so.
try
  FileIO.write(tmp + "/no-such-directory/x.txt", "x")
  println("FAIL: the write succeeded")
catch case e: IOException =>
  println(e.getClass + " " +
          (if e.getMessage.contains("(No such file or directory)") then "No such file or directory"
           else e.getMessage))
