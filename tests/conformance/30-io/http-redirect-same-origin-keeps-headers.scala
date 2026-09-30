// EXPECT: 200 token=secret
// A redirect to the same origin is followed and keeps the caller's headers.
val server = HttpServer(0) { req =>
  if req.path == "/old" then Response.redirect("/new")
  else Response(200, "token=" + req.headers.getOrElse("x-token", "none"))
}
server.startInBackground()
val r = Requests.get("http://127.0.0.1:" + server.port + "/old", Map("X-Token" -> "secret"))
server.stop()
println(r.statusCode.toString + " " + r.text())
