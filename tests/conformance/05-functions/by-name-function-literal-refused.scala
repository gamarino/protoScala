// EXPECT-ERROR: not supported on a function literal
// A function literal has no name at its call sites, and Scala's function types
// carry no by-name parameters either (D47).
val f = (x: => Int) => x
println(f(1))
