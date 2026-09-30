// EXPECT: RequestFailedException 404 missing|404 missing
// requests-scala checks the status by default: a 4xx or 5xx response raises a
// RequestFailedException that carries the response. check = false answers it.
val server = HttpServer(0) { req => Response(404, "missing") }
server.startInBackground()
val u = "http://127.0.0.1:" + server.port + "/nothing"
val raised =
  try { Requests.get(u); "no exception" }
  catch case e: RequestFailedException =>
    e.getClass + " " + e.response.statusCode + " " + e.response.text()
val r = Requests.get(u, check = false)
server.stop()
println(raised + "|" + r.statusCode + " " + r.text())
