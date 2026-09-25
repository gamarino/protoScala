// EXPECT: IllegalArgumentException IllegalArgumentException
// Writes go under PROTOSCALA_TEST_TMP, a directory inside the BUILD tree that
// tests/CMakeLists.txt creates for this fixture. Nothing here touches a path the
// fixture did not create, and the fixture refuses to run at all rather than fall
// back to the working directory -- which would be the source tree.
val tmp = System.getenv("PROTOSCALA_TEST_TMP")
if tmp == "" then throw new IllegalStateException("PROTOSCALA_TEST_TMP is not set")
// A protoScala String can hold a NUL; a POSIX path cannot. Handing such a path to
// the syscall would silently operate on the PREFIX before the NUL -- a different
// file than the program named, which is the one failure here that could do damage
// rather than merely report it.
def failureOf(f: () => Any): String =
  try
    f()
    "no failure at all"
  catch case e: IllegalArgumentException => e.getClass
println(failureOf(() => Source.fromFile(tmp + "/a\u0000b")) + " " +
        failureOf(() => FileIO.write(tmp + "/a\u0000b", "x")))
