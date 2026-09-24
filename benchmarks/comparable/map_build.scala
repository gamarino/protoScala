// EXPECT: 50000 1249975000 50000
// map_build.scala - build a 50000-entry Map keyed by String, read every key
// back, and fold the values. The ROADMAP's benchmark suite v1 "map-build"
// workload (Phase 3). It prints the entry count, the value fold and the number
// of keys read back successfully, so a Map that loses entries fails the run.
// The three numbers were computed with tools/scala3-3.9.0, never by hand.

@main def benchMapBuild(): Unit =
  var m = Map[String, Int]()
  var i = 0
  while i < 50000 do
    m = m + (("k" + i) -> i)
    i += 1
  var total = 0
  var found = 0
  var j = 0
  while j < 50000 do
    val v = m.getOrElse("k" + j, -1)
    if v >= 0 then
      total += v
      found += 1
    j += 1
  println(m.size.toString + " " + total + " " + found)
