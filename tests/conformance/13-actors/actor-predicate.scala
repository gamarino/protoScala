// EXPECT: true false Actor(0)
val a = Actor.spawn(0) { (s, m) => (s, s) }
println(Actor.isActor(a).toString + " " + Actor.isActor(1).toString + " " + a.toString)
