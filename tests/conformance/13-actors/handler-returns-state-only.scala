// EXPECT: 10
// D45: a handler that only updates the state returns the new state, with no
// reply to fabricate. The ask is ordered behind the sends, so it flushes them.
val counter = Actor.spawn(0) { (s, m) => s + m }
counter ! 1
counter ! 2
counter ! 3
val flushed = (counter ? 4).await
println(counter.value)
