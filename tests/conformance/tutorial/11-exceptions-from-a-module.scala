// EXPECT: not a number; legacyArea: negative radius
// A module's own error types are caught by name through the import (D133);
// protoCore's errors arrive as Scala classes, and a C++ std::runtime_error as a
// RuntimeException. Runs with the geometry plug-in (tests/CMakeLists.txt).
import example.geometry as g

@main def run(): Unit =
  def area(r: Int): String =
    try "area " + g.circleArea(r)
    catch
      case e: g.NegativeRadius => "rejected: " + e.message
      case e: g.GeometryError  => "geometry error: " + e.name
  println(area(-1))
  val parsed =
    try g.parseRadius("ten").toString
    catch case e: NumberFormatException => "not a number"
  val legacy =
    try g.legacyArea(-1).toString
    catch case e: RuntimeException => e.getMessage
  println(parsed + "; " + legacy)
