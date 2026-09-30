// EXPECT: 200 hi
// requests-scala is used as `requests.get(url)`; the same object answers under
// that name, so code copied from its documentation runs unchanged.
val server = HttpServer(0) { req => Response(200, "hi") }
server.startInBackground()
val r = requests.get("http://127.0.0.1:" + server.port + "/")
server.stop()
println(r.statusCode.toString + " " + r.text())
