// EXPECT: true false List(a.txt, b.txt, sub) true false true 5 true
// Directories: mkdirs answers true when it created the directory and false when
// one was already there (java.io.File.mkdirs); list answers the entry names,
// sorted; size and lastModified read the file's metadata.
val tmp = System.getenv("PROTOSCALA_TEST_TMP")
if tmp == "" then throw new IllegalStateException("PROTOSCALA_TEST_TMP is not set")
val dir = tmp + "/io-dirs"
FileIO.deleteRecursively(dir)
val made = FileIO.mkdirs(dir + "/sub/deeper")
val again = FileIO.mkdirs(dir + "/sub/deeper")
FileIO.write(dir + "/b.txt", "hello")
FileIO.write(dir + "/a.txt", "")
println(made.toString + " " + again + " " + FileIO.list(dir) + " " +
        FileIO.isDirectory(dir + "/sub") + " " + FileIO.isDirectory(dir + "/b.txt") + " " +
        FileIO.isFile(dir + "/b.txt") + " " + FileIO.size(dir + "/b.txt") + " " +
        (FileIO.lastModified(dir + "/b.txt") > 1000000000000L))
