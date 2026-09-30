// EXPECT: a=1 b=x & y|lang=scala 3|application/x-www-form-urlencoded
// Requests.post with a Map as its data sends a form (application/x-www-form-
// urlencoded) and `params` adds an encoded query string, as in requests-scala;
// the handler reads the form back through `req.form`.
val server = HttpServer(0) { req =>
  Response(200, "a=" + req.form("a") + " b=" + req.form("b") + "|lang=" + req.query("lang") +
                "|" + req.headers("content-type"))
}
server.startInBackground()
val r = Requests.post("http://127.0.0.1:" + server.port + "/form",
                      Map("a" -> "1", "b" -> "x & y"), Map(), Map("lang" -> "scala 3"))
server.stop()
println(r.text())
