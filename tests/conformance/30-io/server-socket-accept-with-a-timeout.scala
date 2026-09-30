// EXPECT: None true SocketException
// tryAccept(ms) answers None when nobody connected in time and Some(socket)
// otherwise; accept() on a closed server socket raises a SocketException, as
// java.net's does ("Socket is closed").
val server = new ServerSocket(0, "127.0.0.1")
val none = server.tryAccept(100)
val client = new Socket("127.0.0.1", server.getLocalPort)
val some = server.tryAccept(5000)
some.foreach(_.close())
client.close()
server.close()
val closed =
  try { server.accept(); "accepted" }
  catch case e: SocketException => e.getClass
println(none.toString + " " + some.isDefined + " " + closed)
