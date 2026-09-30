// EXPECT: ping from 127.0.0.1|pong|None
// UDP: a DatagramSocket bound to a free port sends and receives datagrams.
// receive() answers a Datagram with the data and the sender's address;
// tryReceive(ms) answers None when nothing arrived in time.
val a = new DatagramSocket(0, "127.0.0.1")
val b = new DatagramSocket(0, "127.0.0.1")
a.send("ping", "127.0.0.1", b.getLocalPort)
val d = b.receive()
b.send("pong", d.host, d.port)
val reply = a.receive()
val nothing = a.tryReceive(100)
a.close()
b.close()
println(d.data + " from " + d.host + "|" + reply.data + "|" + nothing)
