// EXPECT: 200 aé€b 7
// A chunked response whose chunks split multi-byte UTF-8 characters: 'é' (C3 A9)
// and '€' (E2 82 AC) are each cut across two chunks. The body is reassembled as
// bytes before it is decoded, so text() answers the characters whole. The server
// is a raw socket, so the bytes on the wire are exactly these.
val server = new ServerSocket(0, "127.0.0.1")
val t = Thread.start { () =>
  val c = server.accept()
  var line = c.readLine()
  while line != null && line != "" do line = c.readLine()
  c.write("HTTP/1.1 200 OK\r\ntransfer-encoding: chunked\r\ncontent-type: text/plain; charset=utf-8\r\n\r\n")
  c.write("2\r\na")
  c.writeBytes(Bytes(0xC3))
  c.write("\r\n")
  c.write("2\r\n")
  c.writeBytes(Bytes(0xA9, 0xE2))
  c.write("\r\n")
  c.write("3\r\n")
  c.writeBytes(Bytes(0x82, 0xAC))
  c.write("b\r\n0\r\n\r\n")
  c.close()
}
val r = Requests.get("http://127.0.0.1:" + server.getLocalPort + "/")
t.join()
server.close()
println(r.statusCode.toString + " " + r.text() + " " + r.bytes.length)
