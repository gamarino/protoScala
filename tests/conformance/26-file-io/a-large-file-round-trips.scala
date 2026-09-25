// EXPECT: 1048576 16384 true
// Writes go under PROTOSCALA_TEST_TMP, a directory inside the BUILD tree that
// tests/CMakeLists.txt creates for this fixture. Nothing here touches a path the
// fixture did not create, and the fixture refuses to run at all rather than fall
// back to the working directory -- which would be the source tree.
val tmp = System.getenv("PROTOSCALA_TEST_TMP")
if tmp == "" then throw new IllegalStateException("PROTOSCALA_TEST_TMP is not set")
// A megabyte, written in one call and read back in one call. The reader fills a
// 64 KiB buffer at a time, so this is the fixture that exercises its loop: a read
// that stopped after the first buffer would answer 65536 here, and one that
// mishandled the final short buffer would answer something just under a million.
val path = tmp + "/a-large-file.txt"
val line = "0123456789abcdef" * 4          // 64 bytes
var text = ""
var i = 0
while i < 16384 do
  text = text + line
  i = i + 1
FileIO.write(path, text)
val back = Source.fromFile(path).mkString
println(back.length + " " + i + " " + (back == text))
FileIO.delete(path)
