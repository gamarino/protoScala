// EXPECT: 7 héllo €
// Writes go under PROTOSCALA_TEST_TMP, a directory inside the BUILD tree that
// tests/CMakeLists.txt creates for this fixture. Nothing here touches a path the
// fixture did not create, and the fixture refuses to run at all rather than fall
// back to the working directory -- which would be the source tree.
val tmp = System.getenv("PROTOSCALA_TEST_TMP")
if tmp == "" then throw new IllegalStateException("PROTOSCALA_TEST_TMP is not set")
// What FileIO writes, Source reads back: the text is encoded as UTF-8 on the way
// out and decoded as UTF-8 on the way in, so a string with a two-byte and a
// three-byte character survives the round trip unchanged. A mismatch in either
// direction would show up here as a MalformedInputException or as mojibake.
val path = tmp + "/round-trip-utf8.txt"
val text = "héllo €"
FileIO.write(path, text)
val back = Source.fromFile(path).mkString
println(back.length + " " + back)
FileIO.delete(path)
