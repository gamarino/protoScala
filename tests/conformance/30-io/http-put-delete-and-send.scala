// EXPECT: PUT:v1|DELETE:|PATCH:p|200:0
// put, delete and the general send(method, url, ...): the method reaches the
// handler as it was written. A HEAD response carries no body.
val server = HttpServer(0) { req => Response(200, req.method + ":" + req.body) }
server.startInBackground()
val u = "http://127.0.0.1:" + server.port + "/r"
val a = Requests.put(u, "v1").text()
val b = Requests.delete(u).text()
val c = Requests.send("PATCH", u, Map(), Map(), "p").text()
val d = Requests.send("HEAD", u)
server.stop()
println(a + "|" + b + "|" + c + "|" + d.statusCode + ":" + d.text().length)
