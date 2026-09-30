// EXPECT: 404
val server = HttpServer(0) { req =>
  req.path match
    case "/hello" => Response(200, "Hello, " + req.query.getOrElse("name", "world"))
    case "/echo"  => Response.json(req.body)
    case _        => Response.notFound()
}
server.startInBackground()
val base = s"http://127.0.0.1:${server.port}"

val hello = requests.get(base + "/hello", Map(), Map("name" -> "Ada"))
println(s"${hello.statusCode} ${hello.text()}")
val echo = requests.post(base + "/echo", """{"answer": 42}""")
println(s"${echo.contentType.get} ${echo.text()}")
val missing =
  try requests.get(base + "/nowhere").statusCode
  catch case e: RequestFailedException => e.response.statusCode
println(missing)
server.stop()
