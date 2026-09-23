// EXPECT: 7 Actor(7)
val acc = Actor.spawn(0) { (state, msg) => (state + msg, state + msg) }
acc ! 3
val total = (acc ? 4).await
println(total.toString + " " + acc.toString)
