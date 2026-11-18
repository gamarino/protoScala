// EXPECT: Int Int Double Char String List Boolean Unit Vector Map Set Range Some Point Function thrown
// Whether `throw` accepts a value is decided by the value's class, for every
// kind of value: Int, BigInt, Double, Char, String, List, Boolean, Unit,
// Vector, Map, Set, Range, Option, case class instances and functions are no
// Throwable, so each is refused with its type named; an exception is thrown.
case class Point(x: Int)

def outcome(v: Any): String =
  try
    throw v.asInstanceOf[Any]
  catch
    case e: IllegalArgumentException => e.getMessage.stripPrefix("throw expects a Throwable, got ")
    case e: RuntimeException => "thrown"

@main def run(): Unit =
  var big: BigInt = 1
  for (i <- 1 to 30) big = big * i
  val values: List[Any] = List(42, big, 2.5, 'c', "s", List(1), true, (), Vector(1),
    Map(1 -> 2), Set(1), 1 to 3, Some(1), Point(1), (x: Int) => x, new RuntimeException("x"))
  println(values.map(outcome).mkString(" "))
