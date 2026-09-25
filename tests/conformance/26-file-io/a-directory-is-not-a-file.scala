// EXPECT-ERROR: FileNotFoundException: _data (Is a directory)
// Reading a directory. On Linux `open` of a directory SUCCEEDS and the failure
// only appears at the first `read`, so an implementation that trusted `open`
// would report something else, or nothing. scalac 3.9.0 raises
// java.io.FileNotFoundException "<path> (Is a directory)", from `fromFile`
// itself; this matches both the class and the message.
println(Source.fromFile("_data").mkString)
