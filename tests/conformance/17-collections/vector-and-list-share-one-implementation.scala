// EXPECT: ok
// Every method of the shared surface, applied to a List and to a Vector of the
// same elements: the results must agree element for element, differing only in
// the wrapper. One implementation is installed on both prototypes, so a
// divergence here would mean the installation, not the algorithm, drifted.
@main def run(): Unit =
  val xs = List(4, 1, 3, 2)
  val v = Vector(4, 1, 3, 2)
  val checks = List(
    (xs.length == v.length),
    (xs.head == v.head),
    (xs.last == v.last),
    (xs.init.toList == v.init.toList),
    (xs.tail.toList == v.tail.toList),
    (xs.take(2).toList == v.take(2).toList),
    (xs.drop(2).toList == v.drop(2).toList),
    (xs.takeWhile(_ > 2).toList == v.takeWhile(_ > 2).toList),
    (xs.dropWhile(_ > 2).toList == v.dropWhile(_ > 2).toList),
    (xs.reverse.toList == v.reverse.toList),
    (xs.map(_ * 2).toList == v.map(_ * 2).toList),
    (xs.filter(_ > 2).toList == v.filter(_ > 2).toList),
    (xs.filterNot(_ > 2).toList == v.filterNot(_ > 2).toList),
    (xs.flatMap(x => List(x, x)).toList == v.flatMap(x => List(x, x)).toList),
    (xs.sorted.toList == v.sorted.toList),
    (xs.sortBy(x => -x).toList == v.sortBy(x => -x).toList),
    (xs.sortWith(_ > _).toList == v.sortWith(_ > _).toList),
    (xs.distinct.toList == v.distinct.toList),
    (xs.zipWithIndex.toList == v.zipWithIndex.toList),
    (xs.zip(xs).toList == v.zip(v).toList),
    (xs.foldLeft(0)(_ + _) == v.foldLeft(0)(_ + _)),
    (xs.foldRight(0)((a, b) => a - b) == v.foldRight(0)((a, b) => a - b)),
    (xs.reduce(_ + _) == v.reduce(_ + _)),
    (xs.sum == v.sum),
    (xs.product == v.product),
    (xs.min == v.min),
    (xs.max == v.max),
    (xs.minBy(x => -x) == v.minBy(x => -x)),
    (xs.maxBy(x => -x) == v.maxBy(x => -x)),
    (xs.count(_ > 2) == v.count(_ > 2)),
    (xs.exists(_ > 3) == v.exists(_ > 3)),
    (xs.forall(_ > 0) == v.forall(_ > 0)),
    (xs.find(_ > 2) == v.find(_ > 2)),
    (xs.indexOf(3) == v.indexOf(3)),
    (xs.contains(3) == v.contains(3)),
    (xs.mkString("-") == v.mkString("-")),
    (xs.headOption == v.headOption),
    (xs.lastOption == v.lastOption),
    (xs.updated(0, 9).toList == v.updated(0, 9).toList),
    (xs.splitAt(2)._1.toList == v.splitAt(2)._1.toList),
    (xs.partition(_ > 2)._2.toList == v.partition(_ > 2)._2.toList),
    ((xs :+ 5).toList == (v :+ 5).toList),
    ((0 +: xs).toList == (0 +: v).toList),
    ((xs ++ xs).toList == (v ++ v).toList))
  println(if checks.forall(b => b) then "ok" else "MISMATCH: " + checks.indexOf(false))
