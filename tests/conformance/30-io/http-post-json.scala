// EXPECT: POST application/json {"name": "Ada", "langs": ["Scala", "Smalltalk"], "note": "naïve"}
// A JSON body, posted with its content type and echoed back by the server. The
// body travels as UTF-8, so the non-ASCII character survives both directions.
val server = HttpServer(0) { req =>
  Response(200, req.method + " " + req.headers("content-type") + " " + req.body,
           Map("Content-Type" -> "application/json"))
}
server.startInBackground()
val json = """{"name": "Ada", "langs": ["Scala", "Smalltalk"], "note": "naïve"}"""
val r = Requests.post("http://127.0.0.1:" + server.port + "/people", json,
                      Map("Content-Type" -> "application/json"))
server.stop()
println(r.text())
