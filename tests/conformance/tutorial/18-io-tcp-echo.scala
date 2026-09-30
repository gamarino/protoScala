// EXPECT: null
val server = new ServerSocket(0, "127.0.0.1")
val worker = Thread.start { () =>
  val conn = server.accept()
  val line = conn.readLine()
  conn.write(line.reverse + "\n")
  conn.close()
}
val client = new Socket("127.0.0.1", server.getLocalPort)
client.write("stressed\n")
println(client.readLine())
println(client.readLine())
client.close()
worker.join()
server.close()
