// EXPECT: Temp(21.0) Temp(100.0)
class Temp(val celsius: Double):
  override def toString = "Temp(" + celsius + ")"
object Temp:
  def apply(c: Double): Temp = new Temp(c)
  def fromF(f: Double): Temp = new Temp((f - 32) * 5 / 9)

@main def run(): Unit =
  println(Temp(21.0).toString + " " + Temp.fromF(212.0))
