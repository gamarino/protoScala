// EXPECT: 84
val adder = Actor.spawn(0) { (s, m) => (s, m * 2) }
val caller = Actor.spawn(0) { (s, m) => {
  val doubled = (adder ? m).await
  (s + doubled, s + doubled)
} }
println((caller ? 42).await)
