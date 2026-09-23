// EXPECT: 30
// The snapshot spans four bytecode frames: the handler and three functions.
val echo = Actor.spawn(0) { (s, m) => (s, m) }
def level3(f: Future[Int]): Int = f.await + 1
def level2(f: Future[Int]): Int = level3(f) + 1
def level1(f: Future[Int]): Int = level2(f) + 1
val caller = Actor.spawn(0) { (s, m) =>
  val r = level1(echo ? 27)
  (r, r)
}
println((caller ? 0).await)
