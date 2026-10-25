var hits = 0

class Counter:
  var count = 0
  def inc(): Unit = count += 1

val c = new Counter()
c.inc()
c.inc()
hits += 1
println(c.count + hits)
