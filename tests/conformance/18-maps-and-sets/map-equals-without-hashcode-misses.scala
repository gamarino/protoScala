// EXPECT: 2 miss 6 miss
// D71. A class that overrides `equals` but not `hashCode` is a value key that
// hashes by identity, so two `==` instances land in different slots and never
// find each other -- the classic equals/hashCode bug.
//
// Scala answers `1 y` for the first half and `6 miss` for the second, because
// its Map1..Map4 representations compare by `==` alone and only a fifth entry
// promotes the map to a hashed one. protoScala is hashed from the first entry,
// so it exposes the defect immediately. Both answers were taken from
// tools/scala3-3.9.0; the divergence is confined to maps of at most four
// entries, and the cost of matching it would be a second, unhashed small-map
// representation that ProtoMap does not offer, to preserve a behaviour Scala
// itself loses at five entries.
class Broken(val n: Int):
  override def equals(o: Any): Boolean =
    o.isInstanceOf[Broken] && o.asInstanceOf[Broken].n == n
@main def run(): Unit =
  val small = Map(new Broken(1) -> "x", new Broken(1) -> "y")
  var big = Map[Broken, String]()
  var i = 0
  while i < 6 do
    big = big + (new Broken(i) -> ("v" + i))
    i += 1
  println(small.size.toString + " " + small.getOrElse(new Broken(1), "miss") + " " +
    big.size + " " + big.getOrElse(new Broken(3), "miss"))
