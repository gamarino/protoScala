// EXPECT: 65
// Five actors, each holding a different number; each is told 10 and then asked
// for its state. The ask is ordered behind the tell, so the sum is exact.
// (1+2+3+4+5) + 5*10 = 65.
val actors = List(1, 2, 3, 4, 5).map(n => Actor.spawn(n) { (s, m) => (s + m, s + m) })
actors.foreach(a => a ! 10)
var total = 0
actors.foreach(a => total += (a ? 0).await)
println(total)
