// EXPECT: geometry errors: 16 of 16 checks passed
// protoCore's unified exception model, end to end, against protoCore's own
// example module (protoCore/examples/exceptions/geometry_module), loaded by the
// plug-in tests/unit/geometry_provider_plugin.cpp and imported through the
// space's resolution chain. The module defines its own error types and raises
// them as proto::ProtoException; a Scala handler names them through the import
// (`case e: g.NegativeRadius`) and selects them by prototype chain (D133).
// protoCore's own errors arrive through the error factory as Scala classes, and
// a C++ std::runtime_error thrown by the module arrives as RuntimeException.
import example.geometry as g
import example.geometry.{GeometryError}

var passed = 0
var total = 0
def check(label: String, ok: Boolean): Unit =
  total += 1
  if ok then passed += 1
  println((if ok then "ok    " else "FAIL  ") + label)

@main def run(): Unit =
  // The module's own subtype, by the name the import gives it.
  val a =
    try { g.circleArea(-1); "returned" }
    catch case e: g.NegativeRadius => "NegativeRadius: " + e.message
  check("circleArea(-1) is caught as g.NegativeRadius",
        a.startsWith("NegativeRadius: circleArea: the radius"))

  // The module's base type, through the path and through a member import.
  val b =
    try { g.circleArea(-1); "returned" }
    catch case e: g.GeometryError => "GeometryError " + e.name
  check("circleArea(-1) is caught as g.GeometryError", b == "GeometryError NegativeRadius")
  val c =
    try { g.circleArea(-1); "returned" }
    catch case e: GeometryError => "imported GeometryError"
  check("circleArea(-1) is caught as an imported GeometryError", c == "imported GeometryError")

  // Handlers for unrelated types do not run; the error leaves them unchanged.
  val d =
    try {
      try { g.circleArea(-1); "returned" }
      catch
        case e: g.DegenerateTriangle => "wrong handler: DegenerateTriangle"
        case e: ArithmeticException => "wrong handler: ArithmeticException"
        case e: RuntimeException => "wrong handler: RuntimeException"
        case e: StackOverflowError => "wrong handler: StackOverflowError"
        case e: Error => "wrong handler: Error"
    } catch case e: g.NegativeRadius => "passed through " + e.name
  check("unrelated handlers do not catch it", d == "passed through NegativeRadius")

  // D134: on the JVM everything thrown is a Throwable, and a module's error is not
  // fatal, so Scala's Exception and Throwable both catch it.
  val ex =
    try { g.circleArea(-1); "returned" }
    catch case e: Exception => "Exception caught " + e.name
  check("case e: Exception catches it", ex == "Exception caught NegativeRadius")
  val th =
    try { g.circleArea(-1); "returned" }
    catch case e: Throwable => "Throwable caught " + e.name
  check("case e: Throwable catches it", th == "Throwable caught NegativeRadius")
  val first =
    try { g.triangleArea(1, 2, 10); "returned" }
    catch
      case e: RuntimeException => "RuntimeException"
      case e: Exception => "Exception"
  check("RuntimeException does not, Exception does", first == "Exception")
  check("it is an instance of Exception outside a catch too",
        (try { g.circleArea(-1); null } catch case e: g.GeometryError => e).isInstanceOf[Exception])

  val t =
    try { g.triangleArea(1, 2, 10); "returned" }
    catch
      case e: g.NegativeRadius => "wrong handler"
      case e: g.DegenerateTriangle => "DegenerateTriangle: " + e.message
  check("triangleArea(1, 2, 10) raises g.DegenerateTriangle",
        t == "DegenerateTriangle: triangleArea: the sides do not make a triangle")
  check("a valid triangle has an area", g.triangleArea(3, 4, 5) == 6.0)

  // protoCore's InvalidArgument, built by the error factory as Scala's class.
  val p =
    try { g.parseRadius("ten"); "returned" }
    catch case e: NumberFormatException => "NumberFormatException"
  check("parseRadius(\"ten\") arrives as NumberFormatException", p == "NumberFormatException")
  val q =
    try { g.parseRadius("ten"); "returned" }
    catch case e: IllegalArgumentException => e.getClass
  check("... which is an IllegalArgumentException", q == "NumberFormatException")

  // The language's own integer division by zero.
  val zero = total - total
  val z =
    try { (1 / zero).toString }
    catch case e: ArithmeticException => e.getMessage
  check("1 / 0 arrives as ArithmeticException", z == "/ by zero")

  // A C++ std::runtime_error from the module: Scala's generic runtime error.
  val l =
    try { g.legacyArea(-1); "returned" }
    catch case e: RuntimeException => e.getMessage
  check("legacyArea(-1) arrives as RuntimeException", l == "legacyArea: negative radius")

  // Throwable.getMessage on a module's error answers its `message` (D136).
  val gm =
    try { g.circleArea(-1); ("returned", "") }
    catch case e: Exception => (e.getMessage, e.message)
  check("getMessage on a module error is its message",
        gm._1 == gm._2 && gm._1.startsWith("circleArea: the radius"))

  // Try captures a module error like any other thrown value.
  check("Try(g.circleArea(-1)) is a Failure", Try(g.circleArea(-1)).isFailure)

  println(s"geometry errors: $passed of $total checks passed")
