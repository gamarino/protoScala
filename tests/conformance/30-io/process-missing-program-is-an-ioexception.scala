// EXPECT: IOException
// A program that cannot be run is an IOException, as it is in scala.sys.process
// ("Cannot run program ..."), and not a silent exit code.
try println(Process(List("protoscala-no-such-program-xyz")).!)
catch case e: IOException => println(e.getClass)
