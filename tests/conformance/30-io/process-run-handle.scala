// EXPECT: 7 143
// `run()` starts the command and answers a handle at once: exitValue() waits for
// the exit code, and destroy() ends the process (SIGTERM, reported as 128 + 15,
// the shell's convention, as the JVM reports it on Linux).
val quick = Process(List("sh", "-c", "exit 7")).run()
val slow = Process(List("sleep", "30")).run()
slow.destroy()
println(quick.exitValue().toString + " " + slow.exitValue())
