// EXPECT: 400|414|431|413|200
// The server refuses malformed or oversized requests before the handler runs,
// with the status the library chooses: a malformed request line (400), a
// request line over 8 KiB (414), a header line over 8 KiB (431), and a declared
// body larger than the server's limit (413). A normal request still works.
val server = HttpServer(0, "127.0.0.1", 1000) { req => Response(200, "fine") }
server.startInBackground()
def statusOf(raw: String): String =
  val s = new Socket("127.0.0.1", server.port)
  s.write(raw)
  val line = s.readLine()
  s.close()
  if line == null then "closed" else line.split(" ")(1)
val longTarget = "/" + ("a" * 9000)
val longHeader = "x-big: " + ("b" * 9000)
println(List(
  statusOf("NOT A VALID REQUEST LINE\r\n\r\n"),
  statusOf("GET " + longTarget + " HTTP/1.1\r\nhost: x\r\n\r\n"),
  statusOf("GET / HTTP/1.1\r\n" + longHeader + "\r\n\r\n"),
  statusOf("POST / HTTP/1.1\r\ncontent-length: 5000\r\n\r\n"),
  statusOf("GET / HTTP/1.1\r\nhost: x\r\n\r\n")).mkString("|"))
server.stop()
