// EXPECT: GET|/a b/c|x y|1|é|3|tester|0
// What a handler receives: the method, the percent-decoded path, the query as a
// Map ('+' is a space, %XX sequences are UTF-8 bytes), the headers under lower-
// case names, and the body.
val server = HttpServer(0) { req =>
  Response(200, req.method + "|" + req.path + "|" + req.query("q") + "|" + req.query("n") +
                "|" + req.query("e") + "|" + req.query.size + "|" + req.headers("x-client") +
                "|" + req.body.length)
}
server.startInBackground()
val r = Requests.get("http://127.0.0.1:" + server.port + "/a%20b/c?q=x+y&n=1&e=%C3%A9",
                     Map("X-Client" -> "tester"))
server.stop()
println(r.text())
