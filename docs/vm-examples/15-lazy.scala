lazy val expensive: Int =
  println("computing")
  42

def twice(x: => Int): Int = x + x

println(expensive + expensive)
println(twice { println("evaluated"); 1 })
