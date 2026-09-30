// EXPECT: SocketTimeoutException true
// setSoTimeout bounds every later read: a peer that sends nothing makes readLine
// raise a SocketTimeoutException (an InterruptedIOException, so an IOException),
// as in java.net.
val server = new ServerSocket(0, "127.0.0.1")
val client = new Socket("127.0.0.1", server.getLocalPort)
val quiet = server.accept()
client.setSoTimeout(200)
try println(client.readLine())
catch case e: SocketTimeoutException =>
  println(e.getClass + " " + e.isInstanceOf[IOException])
client.close()
quiet.close()
server.close()
