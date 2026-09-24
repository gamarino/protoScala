// EXPECT: true Some(null) None
// PROTO_NONE is both Scala `null` and "attribute missing", so presence is
// decided by the nullptr hashedGet returns, never by comparing a result with
// PROTO_NONE.
@main def run(): Unit =
  val m = Map(1 -> null)
  println(m.contains(1).toString + " " + m.get(1) + " " + m.get(2))
