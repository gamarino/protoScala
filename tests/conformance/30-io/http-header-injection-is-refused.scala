// EXPECT: client=IllegalArgumentException server=500
// A header value with a line break could forge extra headers or a whole second
// request. The client refuses to send one (IllegalArgumentException), and a
// handler that answers one gets a plain 500 instead of an injected response.
val server = HttpServer(0) { req => Response(200, "x", Map("X-Evil" -> "a\r\nSet-Cookie: stolen=1")) }
server.startInBackground()
val u = "http://127.0.0.1:" + server.port + "/"
val client =
  try { Requests.get(u, Map("X-Evil" -> "a\r\nInjected: yes")); "sent" }
  catch case e: IllegalArgumentException => e.getClass
val r = Requests.get(u, check = false)
server.stop()
println("client=" + client + " server=" + r.statusCode)
