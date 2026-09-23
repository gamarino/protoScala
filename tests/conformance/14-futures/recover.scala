// EXPECT: -1
val bad = Actor.spawn(0) { (s, m) => (s, s / 0) }
println((bad ? 1).recover(e => -1).await)
