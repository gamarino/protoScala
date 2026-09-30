// EXPECT: ConnectException true
// Connecting to a port nobody listens on is a ConnectException (java.net's
// class for a refused connection), which is also an IOException.
val probe = new ServerSocket(0, "127.0.0.1")
val port = probe.getLocalPort
probe.close()
try new Socket("127.0.0.1", port)
catch case e: ConnectException => println(e.getClass + " " + e.isInstanceOf[IOException])
