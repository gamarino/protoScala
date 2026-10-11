// The garbage for the adaptive heap checks (adaptive_heap_check.py,
// tests/CMakeLists.txt): each round builds a 50-element List and drops it, and
// nothing is kept. The first argument is the number of rounds. The last line
// is the sentinel the check requires, with the work done, so a run that
// stopped early cannot pass. Tuples are avoided: they are interned and
// perennial, so they are not garbage.

def oneRound(i: Int): Int =
  var xs = List[Int]()
  var j = 0
  while j < 50 do
    xs = (i + j) :: xs
    j += 1
  xs.length

@main def run(args: String*): Unit =
  val rounds = if args.length > 0 then args(0).toInt else 20000
  var made = 0
  var i = 0
  while i < rounds do
    made += oneRound(i)
    i += 1
  println("adaptive heap garbage done: rounds=" + rounds + " objects=" + made)
