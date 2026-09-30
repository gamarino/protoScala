// EXPECT: 500 Internal Server Error|200 still serving
// An exception in the handler answers a plain 500 (and is reported on standard
// error); the server keeps serving the next request.
val server = HttpServer(0) { req =>
  if req.path == "/boom" then throw new IllegalStateException("boom")
  else Response(200, "still serving")
}
server.startInBackground()
val base = "http://127.0.0.1:" + server.port
val bad = Requests.get(base + "/boom", check = false)
val good = Requests.get(base + "/ok")
server.stop()
println(bad.statusCode.toString + " " + bad.text() + "|" + good.statusCode + " " + good.text())
