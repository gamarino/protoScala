// EXPECT: 3
// A plain by-name constructor parameter is a private field holding the thunk the
// `new` site built, so each read re-evaluates it (D47), exactly as scalac 3.9
// prints 3 for this program.
class Holder(v: => Int):
  def twice: Int = v + v
var k = 0
println(new Holder({ k = k + 1; k }).twice)
