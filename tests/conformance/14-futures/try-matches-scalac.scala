// EXPECT: true false 7 | false true | 7 -1 | Some(7) None | Some(Success(7))
// Every value on this line was verified against scalac 3.9.0 running the same
// program with scala.util.Try. Since Phase 4 the payload is a real Throwable,
// exactly as Scala's is, so there is no longer any difference to note (D44 was
// retired when RuntimeError was removed from the prelude).
val s: Try[Int] = Success(7)
val f: Try[Int] = Failure(new RuntimeException("boom"))
println(
  s.isSuccess.toString + " " + s.isFailure.toString + " " + s.get.toString + " | " +
  f.isSuccess.toString + " " + f.isFailure.toString + " | " +
  s.getOrElse(0).toString + " " + f.getOrElse(-1).toString + " | " +
  s.toOption.toString + " " + f.toOption.toString + " | " +
  Some(s).toString)
