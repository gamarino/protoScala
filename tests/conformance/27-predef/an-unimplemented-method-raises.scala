// EXPECT: NotImplementedError: an implementation is missing
// `???` stands in for a body that is not written yet. scalac 3.9.0 prints
// `scala.NotImplementedError: an implementation is missing`.
def area(shape: String): Double = ???
@main def run(): Unit =
  try println(area("circle"))
  catch case e: NotImplementedError => println(e.toString)
