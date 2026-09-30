// EXPECT: 200 OK|hello /greet|List(text/plain; charset=utf-8)|true
// An HTTP server and client in one program. HttpServer(port) { req => ... }
// serves each request on the actor pool; Requests.get answers a response with
// requests-scala's surface: statusCode, statusMessage, text(), headers (lower-
// case names, each with the list of its values).
val server = HttpServer(0) { req => Response(200, "hello " + req.path) }
server.startInBackground()
val r = Requests.get("http://127.0.0.1:" + server.port + "/greet")
server.stop()
println(r.statusCode.toString + " " + r.statusMessage + "|" + r.text() + "|" +
        r.headers("content-type") + "|" + r.is2xx)
