// EXPECT: 200 token=none
// A redirect to ANOTHER origin (here another port) is followed, but the caller's
// headers are dropped for it: a credential meant for one server must not be
// handed to whichever server the first one names.
val other = HttpServer(0) { req =>
  Response(200, "token=" + req.headers.getOrElse("x-token", "none"))
}
other.startInBackground()
val first = HttpServer(0) { req => Response.redirect("http://127.0.0.1:" + other.port + "/landing") }
first.startInBackground()
val r = Requests.get("http://127.0.0.1:" + first.port + "/go", Map("X-Token" -> "secret"))
first.stop()
other.stop()
println(r.statusCode.toString + " " + r.text())
