// EXPECT: 25 -1
val echo = Actor.spawn(0) { (s, m) => (s, m) }
val chained = (echo ? 4).map(x => x + 1).flatMap(x => Future(x * 5))
val recovered = Future(1 / 0).recover(e => -1)
println(chained.await.toString + " " + recovered.await.toString)
