// EXPECT: even:5 odd empty nonempty:3 a->b none
object Even {
  def unapply(n: Int): Option[Int] = if n % 2 == 0 then Some(n / 2) else None
}
object NonEmpty {
  def unapply(s: String): Boolean = s.nonEmpty
}
object Split {
  def unapply(s: String): Option[(String, String)] = {
    val i = s.indexOf("=")
    if i < 0 then None else Some((s.substring(0, i), s.substring(i + 1)))
  }
}
def num(n: Int): String = n match {
  case Even(half) => "even:" + half
  case _ => "odd"
}
def str(s: String): String = s match {
  case NonEmpty() => "nonempty:" + s.length
  case _ => "empty"
}
def kv(s: String): String = s match {
  case Split(k, v) => k + "->" + v
  case _ => "none"
}

@main def run(): Unit = {
  println(num(10) + " " + num(3) + " " + str("") + " " + str("abc") + " " + kv("a=b") + " " + kv("ab"))
}
