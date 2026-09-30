// EXPECT: Bytes(0, -1, 10)|héllo|wörld
// writeBytes / readBytes carry binary data; read(n) answers up to n whole
// characters, never half of a multi-byte one.
val server = ServerSocket(0, "127.0.0.1")
val t = Thread.start { () =>
  val c = server.accept()
  c.writeBytes(Bytes(0, 255, 10))
  c.write("héllowörld")
  c.close()
}
val s = Socket("127.0.0.1", server.localPort)
val bytes = s.readBytes(3)
val first = s.read(5)
val second = s.read(100)
s.close()
t.join()
server.close()
println(bytes.toString + "|" + first + "|" + second)
