// EXPECT: 6
// Three suspensions in one message, so the resume path installs a new snapshot
// each time round the loop.
val echo = Actor.spawn(0) { (s, m) => (s, m) }
val caller = Actor.spawn(0) { (s, m) =>
  var total = 0
  var i = 1
  while i <= 3 do
    total += (echo ? i).await
    i += 1
  (total, total)
}
println((caller ? 0).await)
