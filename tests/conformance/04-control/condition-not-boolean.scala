// EXPECT-ERROR: ClassCastException
// D4: Scala rejects this at compile time; protoScala reports it when it runs.
@main def run(): Unit =
  if 1 then println("never")
