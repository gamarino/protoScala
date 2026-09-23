// EXPECT: 42
case class Increment(by: Int)
case object GetValue
val counter = Actor.spawn(0) { (state, msg) =>
  msg match
    case Increment(by) => (state + by, state + by)
    case GetValue      => (state, state)
}
counter ! Increment(40)
counter ! Increment(2)
var seen = 0
while seen < 42 do
  seen = counter.value
println(seen)
