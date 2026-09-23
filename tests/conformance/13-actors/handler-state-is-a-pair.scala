// EXPECT: (1,2)
// D45: a Tuple2 result is always read as (newState, reply). An actor whose
// state is itself a pair therefore returns it inside the pair form.
val a = Actor.spawn((0, 0)) { (s, m) => ((s._1 + 1, s._2 + m), m) }
val reply = (a ? 2).await
println(a.value)
