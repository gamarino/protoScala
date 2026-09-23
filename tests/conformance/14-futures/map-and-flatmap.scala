// EXPECT: 25
val echo = Actor.spawn(0) { (s, m) => (s, m) }
val f = (echo ? 4).map(x => x + 1).flatMap(x => Future(x * 5))
println(f.await)
