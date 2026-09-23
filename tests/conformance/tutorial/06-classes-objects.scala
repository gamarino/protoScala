// EXPECT: Temp(21.0) Temp(100.0) 2
class Temp(val celsius: Double):
  override def toString = "Temp(" + celsius + ")"

object Temp:
  var made = 0
  def apply(c: Double): Temp =
    made += 1
    new Temp(c)
  def fromF(f: Double): Temp = apply((f - 32) * 5 / 9)

@main def run(): Unit =
  println(Temp(21.0).toString + " " + Temp.fromF(212.0) + " " + Temp.made)
