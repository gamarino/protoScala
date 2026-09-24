// EXPECT-ERROR: throw expects a Throwable, got Int
// `throw` accepts only a Throwable, which is Scala's rule. Types are erased
// here (D4), so scalac rejects this program at compile time and protoScala
// detects it at the THROW instruction — late DETECTION, never silent failure:
// the message names what was expected and what arrived.
// `asInstanceOf[Any]` is the cast that emits no test; a cast to a named class
// (`5.asInstanceOf[RuntimeException]`) fails earlier, with a ClassCastException.
@main def run(): Unit =
  throw 5.asInstanceOf[Any]
