// EXPECT-ERROR: forward reference to value limit extends over the definition of value limit
def m() =
  def loop(i: Int): Int = if i > limit then i else loop(i + 1)
  val limit = 10
  loop(0)
println(m())
