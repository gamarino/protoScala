def show(x: Any): String = x match
  case n: Int => s"int $n"
  case h :: t => s"list starting with $h"
  case (a, b) => s"pair of $a and $b"
  case _ => "something else"

println(show(1))
println(show(List(7, 8)))
println(show((1, "one")))
println(1.asInstanceOf[Int] + -(2))
println(!(1 == 2))
