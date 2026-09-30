// EXPECT: 80 requests, 80 ok, 80 distinct
// Eight client threads send ten requests each to one server at the same time.
// Every request gets its own answer: the server serves connections concurrently
// on the actor pool and no response goes to the wrong client.
val server = HttpServer(0) { req => Response(200, "n=" + req.query("n")) }
server.startInBackground()
val base = "http://127.0.0.1:" + server.port + "/?n="
val results = Actor.spawn(List[String]()) { (s, m) =>
  m match
    case x: String => (x :: s, ())
    case _: Int => (s, s)
}
val threads = (0 until 8).toList.map { i =>
  Thread.start { () =>
    var k = 0
    while k < 10 do
      val n = i * 10 + k
      val r = Requests.get(base + n)
      results ! (if r.text() == "n=" + n then "ok" + n else "wrong" + n)
      k += 1
  }
}
threads.foreach(t => t.join())
server.stop()
val all = (results ? 0).await
val oks = all.filter(_.startsWith("ok"))
println(all.length.toString + " requests, " + oks.length + " ok, " + oks.distinct.length + " distinct")
