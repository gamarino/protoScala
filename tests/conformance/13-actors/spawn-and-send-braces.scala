// EXPECT: 10
val counter = Actor.spawn(0) { (state, msg) =>
  (state + msg, state + msg)
}
var i = 0
while (i < 10) {
  counter ! 1
  i += 1
}
// The single-method invariant makes this exact once every message is drained.
var seen = -1
while (seen < 10) {
  seen = counter.value
}
println(counter.value)
