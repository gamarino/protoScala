// EXPECT: UnknownHostException true
// A host name that does not resolve is an UnknownHostException. The `.invalid`
// top-level domain is reserved (RFC 2606) and never resolves.
try new Socket("no-such-host.invalid", 80)
catch case e: UnknownHostException => println(e.getClass + " " + e.isInstanceOf[IOException])
