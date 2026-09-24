// EXPECT: ArithmeticException: / by zero | IndexOutOfBoundsException | NumberFormatException | NoSuchElementException: None.get
// What the runtime raises on its own, each a real exception value of the class its
// name means.
@main def run(): Unit =
  def classOf(body: () => Any): String =
    try { body(); "no error" } catch case e: Throwable => e.getClass
  def report(body: () => Any): String =
    try { body(); "no error" } catch case e: Throwable => e.toString
  println(report(() => 1 / 0) + " | " + classOf(() => List(1)(9)) + " | " +
    classOf(() => "abc".toInt) + " | " + report(() => None.get))
