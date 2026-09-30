// EXPECT: 201 made:ada|x=1
// requests-scala is written with named arguments, and so is this client:
// data =, headers =, params = and check = may come in any order.
val server = HttpServer(0) { req =>
  Response(201, "made:" + req.body + "|x=" + req.query("x"))
}
server.startInBackground()
val r = Requests.post(
  "http://127.0.0.1:" + server.port + "/users",
  params = Map("x" -> "1"),
  data = "ada",
  check = true)
server.stop()
println(r.statusCode.toString + " " + r.text())
