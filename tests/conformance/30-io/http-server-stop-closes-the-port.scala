// EXPECT: 200 ConnectException
// stop() closes the listening socket: a request after it is refused.
val server = HttpServer(0) { req => Response(200, "up") }
server.startInBackground()
val u = "http://127.0.0.1:" + server.port + "/"
val before = Requests.get(u).statusCode
server.stop()
val after =
  try { Requests.get(u); "still up" }
  catch case e: ConnectException => e.getClass
println(before.toString + " " + after)
