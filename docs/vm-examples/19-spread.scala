def total(xs: Int*): Int = xs.sum

case class Bag(items: Int*)

val nums = List(1, 2, 3)
println(total(nums*))
println(Bag(nums*).items)
