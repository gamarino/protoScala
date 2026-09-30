// EXPECT: 0 1
// Two megabytes fed to `true`, which exits without reading any of it. Writing to
// a pipe whose reader is gone raises SIGPIPE, whose default action would kill
// this process; the program must survive and report the child's exit code.
// `cat` then proves the same input also arrives whole when it is read.
val big = "x" * (2 * 1024 * 1024)
val code = (Process(List("true")) #< big).!
val echoed = (Process(List("wc", "-c")) #< big).!!.trim
println(code.toString + " " + (if echoed == (2 * 1024 * 1024).toString then 1 else 0))
