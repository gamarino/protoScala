// EXPECT: 4 Bytes(0, 1, -2, -1) application/octet-stream
// A binary response: the handler answers Bytes, and the client reads them back
// unchanged through `bytes`.
val server = HttpServer(0) { req =>
  Response(200, Bytes(0, 1, 254, 255), Map("Content-Type" -> "application/octet-stream"))
}
server.startInBackground()
val r = Requests.get("http://127.0.0.1:" + server.port + "/bin")
server.stop()
println(r.bytes.length.toString + " " + r.bytes + " " + r.headers("content-type").head)
