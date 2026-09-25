// EXPECT: App body ran
// `object Main extends App` is Scala's older entry point: initialising the
// object runs the program. It is deprecated in Scala 3 in favour of `@main` and
// still works here, because it is what most Scala teaching material writes
// (D104). Nothing references Demo, and it still runs.
object Demo extends App:
  println("App body ran")
