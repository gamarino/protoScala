// EXPECT-ERROR: ClassCastException
// D4: no static type checker; scalac rejects this program, protoScala fails when it runs.
@main def run(): Unit =
  val n = 1
  if n then println("never")
