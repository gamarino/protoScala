// EXPECT: FileNotFoundException FileNotFoundException FileNotFoundException IOException
// Asking for the size, the modification time or the entries of something that
// is not there raises, with the path in the message: an answer of 0 (which is
// what java.io.File gives) would be a swallowed error.
val tmp = System.getenv("PROTOSCALA_TEST_TMP")
if tmp == "" then throw new IllegalStateException("PROTOSCALA_TEST_TMP is not set")
val missing = tmp + "/io-no-such-file"
FileIO.write(tmp + "/io-a-plain-file", "x")
def failure(body: => Any): String =
  try { body; "no failure" }
  catch case e: IOException => e.getClass
println(failure(FileIO.size(missing)) + " " + failure(FileIO.lastModified(missing)) + " " +
        failure(FileIO.list(missing)) + " " + failure(FileIO.list(tmp + "/io-a-plain-file")))
