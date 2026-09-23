// EXPECT: 2000 true
val counter = Actor.spawn(0) { (s, m) => (s + m, s + m) }
val started = System.nanoTime()
val threads = List(1, 2).map { _ =>
  Thread.start { () =>
    var i = 0
    while i < 1000 do
      counter ! 1
      i += 1
  }
}
threads.foreach(t => t.join())
val total = (counter ? 0).await
println(total.toString + " " + (System.nanoTime() >= started).toString)
