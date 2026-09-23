// EXPECT: 5
// `O.m(e)` names the declaration, so a by-name parameter of a method on an
// object is honoured and the unused fallback never runs (D47).
object Guards:
  def orElse(v: Int, fallback: => Int): Int = if v > 0 then v else fallback
println(Guards.orElse(5, { println("computed"); -1 }))
