// EXPECT: true false 7 | false true | 7 -1 | Some(7) None | Some(Success(7))
// The Try/Success/Failure prelude moved up from Phase 3 (D44). Every value on
// this line was verified against scalac 3.9.0 running the same program with
// scala.util.Try (the only difference is the error value: Scala's Failure
// holds a Throwable, protoScala's holds a RuntimeError until Phase 4).
val s: Try[Int] = Success(7)
val f: Try[Int] = Failure(RuntimeError("RuntimeException", "boom"))
println(
  s.isSuccess.toString + " " + s.isFailure.toString + " " + s.get.toString + " | " +
  f.isSuccess.toString + " " + f.isFailure.toString + " | " +
  s.getOrElse(0).toString + " " + f.getOrElse(-1).toString + " | " +
  s.toOption.toString + " " + f.toOption.toString + " | " +
  Some(s).toString)
