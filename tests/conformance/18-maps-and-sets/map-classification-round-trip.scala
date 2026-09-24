// EXPECT: 9 9 ok
// One key of every kind DESIGN §6.1 classifies, inserted and then read back: a
// plain class instance and an `object` on the identity path; true, a Char, an
// Int, a String, a case class, a tuple and a List on the value path. A key whose
// classification is wrong vanishes from the map, so `size` and the read-back
// count disagree and the last field prints BAD. This is the fixture that turns a
// silent misclassification into a visible failure. 'a' and 7 are deliberately
// NOT numerically equal ('a' is 97), so a collision between them would itself be
// a failure. Verified against tools/scala3-3.9.0.
class Plain(val n: Int)
case class Pair(a: Int, b: String)
object Marker
@main def run(): Unit =
  val plain = new Plain(1)
  val keys: List[Any] =
    List(plain, Marker, true, 'a', 7, "s", Pair(1, "x"), (1, 2), List(1, 2))
  var m = Map[Any, Int]()
  var i = 0
  while i < keys.length do
    m = m + (keys(i) -> i)
    i += 1
  var found = 0
  var j = 0
  while j < keys.length do
    if m.getOrElse(keys(j), -1) == j then found += 1
    j += 1
  println(m.size.toString + " " + found + " " +
    (if m.size == keys.length && found == keys.length then "ok" else "BAD"))
