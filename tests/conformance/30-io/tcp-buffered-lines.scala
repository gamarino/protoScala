// EXPECT: one|two|three|four|ab|cd|null
// Lines that arrived in one segment are answered from the socket's read-ahead
// buffer, one per readLine, in order; read(n) and readLine share that buffer,
// and a line that needs more input waits for it.
val server = new ServerSocket(0, "127.0.0.1")
val port = server.getLocalPort
val t = Thread.start { () =>
  val conn = server.accept()
  conn.write("one\ntwo\r\nthree\nfour\nabcd\n")
  conn.close()
}
val client = new Socket("127.0.0.1", port)
val a = client.readLine()
val b = client.readLine()
val c = client.readLine()
val d = client.readLine()
val e = client.read(2)
val f = client.readLine()
val g = client.readLine()
client.close()
t.join()
server.close()
println(List(a, b, c, d, e, f, g).mkString("|"))
