// EXPECT: 84
// Cooperative await: the worker is released while the handler waits, so this
// completes with a single worker, which a blocking await never could.
val adder = Actor.spawn(0) { (s, m) => (s, m * 2) }
val caller = Actor.spawn(0) { (s, m) =>
  val doubled = (adder ? m).await
  (s + doubled, s + doubled)
}
println((caller ? 42).await)
