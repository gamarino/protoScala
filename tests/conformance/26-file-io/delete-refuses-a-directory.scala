// EXPECT: IOException: true
// Writes go under PROTOSCALA_TEST_TMP, a directory inside the BUILD tree that
// tests/CMakeLists.txt creates for this fixture. Nothing here touches a path the
// fixture did not create, and the fixture refuses to run at all rather than fall
// back to the working directory -- which would be the source tree.
val tmp = System.getenv("PROTOSCALA_TEST_TMP")
if tmp == "" then throw new IllegalStateException("PROTOSCALA_TEST_TMP is not set")
// `delete` removes a file. Asked to remove a directory it must RAISE, not answer
// `false`: `false` means "there was nothing there", and a program that treated a
// refusal as an absence would carry on with a wrong picture of the filesystem.
//
// The directory used is PROTOSCALA_TEST_TMP itself -- created by the build, and
// the only directory this fixture has any business naming. The second half of the
// output checks it is still there, so a `delete` that removed it would fail this
// fixture rather than quietly break the next one.
try
  FileIO.delete(tmp)
  println("FAIL: a directory was deleted")
catch case e: IOException => println(e.getClass + ": " + FileIO.exists(tmp))
