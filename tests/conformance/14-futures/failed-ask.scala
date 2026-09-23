// EXPECT: ArithmeticException: / by zero
val bad = Actor.spawn(0) { (s, m) => (s, s / 0) }
val f = bad ? 1
while !f.isCompleted do ()
f.value match
  case Some(Failure(e)) => println(e.toString)
  case other            => println("unexpected: " + other.toString)
