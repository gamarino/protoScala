// EXPECT: handled boom
// `catch h` with a total function is accepted and rewritten to
// `case e => h(e)`, which is what Scala's catch of a PartialFunction means for a
// total function. A genuine PartialFunction rethrows in Scala and raises
// MatchError here, so prefer `case` clauses.
val handle: Throwable => Unit = e => println("handled " + e.getMessage)
@main def run(): Unit =
  try throw new RuntimeException("boom")
  catch handle
