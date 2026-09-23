// EXPECT: ArithmeticException 0
val fragile = Actor.spawn(0) { (s, m) => (s, s / m) }
val f = fragile ? 0
while !f.isCompleted do ()
val name = f.value match
  case Some(Failure(e)) => e.className
  case other            => "none"
println(name + " " + fragile.value.toString)
