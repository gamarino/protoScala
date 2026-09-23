// EXPECT: 42
// D47: `Future` takes its body by name, so the Scala block form works and the
// body runs on the worker pool, not on the caller.
val f = Future {
  val x = 6
  x * 7
}
println(f.await)
