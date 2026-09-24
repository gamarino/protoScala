// EXPECT: ok 200
// The same rooting question with a SECOND thread driving collections: while the
// main thread throws through deep chains, a worker allocates continuously, so
// collection cycles land at arbitrary points of an unwinding rather than only
// where the unwinding itself triggers them.
def churn(n: Int): Int =
  var k = 0
  var acc = 0
  while k < n do
    acc = acc + ("s" + k).length
    k += 1
  acc
def deep(n: Int): Int =
  if n == 0 then throw new IllegalStateException("payload")
  else deep(n - 1) + ("y" + n).length
@main def run(): Unit =
  val t = Thread.start(() => churn(200000))
  var ok = 0
  var i = 0
  while i < 200 do
    try deep(40)
    catch case e: IllegalStateException => if e.getMessage == "payload" then ok += 1
    i += 1
  t.join()
  println("ok " + ok)
