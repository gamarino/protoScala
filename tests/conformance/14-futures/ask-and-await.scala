// EXPECT: 10
val counter = Actor.spawn(0) { (state, msg) =>
  (state + msg, state + msg)
}
counter ! 4
val f = counter ? 6
println(f.await)
