// EXPECT: an actor and a Future recurse as deep as the main program
// Every thread protoScala starts for actors and Futures gets the evaluator's
// 32 MiB stack, on every platform (protoCore's ProtoSpace::setThreadStackBytes),
// so a recursion reaches about the same depth there as in the main program
// before StackOverflowError. Windows' default of 1 MiB would stop it at about a
// fortieth of that depth. `await` is not used, so the fixture also runs
// transpiled (D113).
def depth(n: Int): Int =
  try depth(n + 1)
  catch case e: StackOverflowError => n

def settled(f: Future[Int]): Int =
  while !f.isCompleted do ()
  f.value match
    case Some(Success(d)) => d
    case other            => -1

val onMain = depth(0)
val inFuture = settled(Future(depth(0)))
val actor = Actor.spawn(0) { (s, m) => (s, depth(0)) }
val inActor = settled(actor ? 1)

// The workers' own frames below the body cost a little stack, hence 9/10.
def asDeep(d: Int): Boolean = d * 10 >= onMain * 9
if onMain > 1000 && asDeep(inFuture) && asDeep(inActor) then
  println("an actor and a Future recurse as deep as the main program")
else
  println(s"main $onMain, Future $inFuture, actor $inActor")
