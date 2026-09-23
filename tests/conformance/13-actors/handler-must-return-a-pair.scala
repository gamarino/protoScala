// EXPECT: 0
// A handler that does not return (newState, reply) fails the message; the
// actor keeps its state and stays alive (D45, DESIGN §8.4).
val a = Actor.spawn(0) { (s, m) => s }
a ! 1
a ! 2
println(a.value)
