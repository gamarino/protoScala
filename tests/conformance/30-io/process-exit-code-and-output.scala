// EXPECT: 3|hello world|0|abc
// scala.sys.process's shape: `!` runs the command and answers its exit code,
// `!!` answers what it wrote to standard output, and `#<` feeds it input. The
// command is a List or a Vector of words: protoScala has no Seq (D65).
val code = Process(List("sh", "-c", "exit 3")).!
val out = Process(List("echo", "hello", "world")).!!
val ok = Process(Vector("true")).!
val fed = (Process(List("cat")) #< "abc").!!
println(code.toString + "|" + out.trim + "|" + ok + "|" + fed)
