// EXPECT: RuntimeException: Nonzero exit value: 2
// `!!` throws when the command fails, with scala.sys.process's exact message,
// because the output of a failed command is rarely what the caller wanted.
try println(Process(List("sh", "-c", "exit 2")).!!)
catch case e: RuntimeException => println(e.getClass + ": " + e.getMessage)
