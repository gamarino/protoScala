// EXPECT: echo: hello|echo: ünïcode|true|null
// A TCP server and client over the loopback interface. The server runs on a
// Thread and answers two lines; readLine answers a line without its terminator,
// and null once the peer has closed the connection, as java.io.BufferedReader
// does.
val server = new ServerSocket(0, "127.0.0.1")
val port = server.getLocalPort
val t = Thread.start { () =>
  val conn = server.accept()
  var i = 0
  while i < 2 do
    conn.write("echo: " + conn.readLine() + "\n")
    i += 1
  conn.close()
}
val client = new Socket("127.0.0.1", port)
client.write("hello\n")
val a = client.readLine()
client.write("ünïcode\n")
val b = client.readLine()
val ports = client.getPort == port && client.getLocalPort > 0
val end = client.readLine()
client.close()
t.join()
server.close()
println(a + "|" + b + "|" + ports + "|" + end)
