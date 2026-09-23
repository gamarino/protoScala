// EXPECT: 10
val counter = Actor.spawn(0) { (state, msg) =>
  (state + msg, state + msg)
}
var i = 0
while i < 10 do
  counter ! 1
  i += 1
var seen = 0
while seen < 10 do
  seen = counter.value
println(seen)
