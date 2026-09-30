// EXPECT: SocketTimeoutException
// startTls against a listener that never answers: the handshake is bounded by
// the socket's timeout and raises SocketTimeoutException instead of hanging.
val silent = new ServerSocket(0, "127.0.0.1")
val s = new Socket("127.0.0.1", silent.getLocalPort)
s.setSoTimeout(300)
try s.startTls("localhost")
catch case e: SocketTimeoutException => println(e.getClass)
s.close()
silent.close()
