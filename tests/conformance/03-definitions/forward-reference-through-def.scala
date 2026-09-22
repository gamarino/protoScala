// EXPECT-ERROR: forward reference to value z extends over the definition of value r
def m() =
  def g() = z + 1
  val r = g()
  val z = 1
  r
println(m())
