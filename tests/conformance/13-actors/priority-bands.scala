// EXPECT: start high low
// The handler of the first message announces itself through a global and then
// holds the actor's claim for half a second, so the Low and High messages are
// queued while it runs and land in the same batch: the bands alone decide the
// order they are handled in (DESIGN §8.2). No step here depends on timing.
var started = false
val log = Actor.spawn("") { (state, msg) =>
  if msg == "start" then
    started = true
    val t = System.nanoTime()
    while System.nanoTime() - t < 500000000 do ()
  val next = state + msg + " "
  (next, next)
}
log ! "start"
while !started do ()
log.send("low", Priority.Low)
log.send("high", Priority.High)
var seen = ""
while !seen.contains("low") do
  seen = log.value
println(seen.trim)
