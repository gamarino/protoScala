// EXPECT: one|false|one|FileAlreadyExistsException|two|true
// move and copy follow java.nio.file.Files: an existing target is refused with a
// FileAlreadyExistsException unless the caller asks for it to be replaced.
val tmp = System.getenv("PROTOSCALA_TEST_TMP")
if tmp == "" then throw new IllegalStateException("PROTOSCALA_TEST_TMP is not set")
val dir = tmp + "/io-move"
FileIO.deleteRecursively(dir)
FileIO.mkdirs(dir)
FileIO.write(dir + "/a", "one")
FileIO.move(dir + "/a", dir + "/b")
val moved = Source.fromFile(dir + "/b").mkString
FileIO.copy(dir + "/b", dir + "/c")
val copied = Source.fromFile(dir + "/c").mkString
FileIO.write(dir + "/d", "two")
val refused =
  try { FileIO.copy(dir + "/d", dir + "/c"); "copied over" }
  catch case e: FileAlreadyExistsException => e.getClass
FileIO.copy(dir + "/d", dir + "/c", true)
println(moved + "|" + FileIO.exists(dir + "/a") + "|" + copied + "|" + refused + "|" +
        Source.fromFile(dir + "/c").mkString + "|" + FileIO.exists(dir + "/d"))
