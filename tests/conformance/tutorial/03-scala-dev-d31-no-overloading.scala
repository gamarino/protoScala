// EXPECT-ERROR: f is already defined in Calc
class Calc:
  def f(x: Int) = x
  def f(x: String) = x.length
