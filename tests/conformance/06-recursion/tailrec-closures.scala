// EXPECT: List(10, 20, 30) List(101, 102, 103)
// A closure created in one iteration keeps the parameter value of that
// iteration, and a local `var` it captures is a fresh variable per iteration,
// exactly as with real calls (scalac 3.9 prints the same two lists).
@annotation.tailrec
def collect(n: Int, acc: List[() => Int]): List[Int] =
  if n == 0 then acc.map(f => f()) else collect(n - 1, (() => n * 10) :: acc)

@annotation.tailrec
def withVar(n: Int, acc: List[() => Int]): List[Int] =
  var k = n
  val f = () => k
  k += 100
  if n == 0 then acc.map(g => g()) else withVar(n - 1, f :: acc)

@main def run(): Unit =
  println(s"${collect(3, Nil)} ${withVar(3, Nil)}")
