// EXPECT: List(200, 413, 500)
val server = HttpServer(0, "127.0.0.1", 16) { req =>
  if req.path == "/boom" then throw new IllegalStateException("boom")
  Response.text("fine")
}
server.startInBackground()
val base = s"http://127.0.0.1:${server.port}"
def status(path: String, body: String): Int =
  Requests.post(base + path, body, Map(), Map(), 30000, 5, false).statusCode
println(List(status("/ok", "small"), status("/ok", "x" * 100), status("/boom", "")))
server.stop()
