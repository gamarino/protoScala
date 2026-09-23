// EXPECT: 1000
val counter = Actor.spawn(0) { (s, m) => (s + m, s + m) }
var i = 0
while i < 999 do
  counter ! 1
  i += 1
// The ask is queued last on the same band, and the single-method invariant
// processes a band in order, so its reply is the final count.
println((counter ? 1).await)
