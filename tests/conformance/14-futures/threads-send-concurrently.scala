// EXPECT: 4000
val counter = Actor.spawn(0) { (s, m) => (s + m, s + m) }
val threads = List(1, 2, 3, 4).map { _ =>
  Thread.start { () =>
    var i = 0
    while i < 1000 do
      counter ! 1
      i += 1
  }
}
threads.foreach(t => t.join())
println((counter ? 0).await)
