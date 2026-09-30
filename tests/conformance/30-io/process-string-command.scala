// EXPECT: 0|a b|a b
// A command written as one String is split on spaces, as scala.sys.process
// splits it, and `"cmd".!` / `"cmd".!!` work on a String directly.
val code = "true".!
val out = "echo a b".!!
val built = Process("echo a b").!!
println(code.toString + "|" + out.trim + "|" + built.trim)
