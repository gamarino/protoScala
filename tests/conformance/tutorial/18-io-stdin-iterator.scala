// EXPECT: sum = 0
val numbers = Source.stdin.getLines().map(l => l.trim).filter(l => l.nonEmpty).map(l => l.toInt)
println(s"sum = ${numbers.sum}")
