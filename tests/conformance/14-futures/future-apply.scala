// EXPECT: 9
// Future takes its body by name: `Future(expr)`, as in Scala (D47).
val f = Future(3 * 3)
println(f.await)
