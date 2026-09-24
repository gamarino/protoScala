// EXPECT: High 0 Medium 1 Low 2 3
// D52 retired: `Priority` is a real prelude `enum` whose ordinals are 0 / 1 / 2,
// which is exactly the band index the scheduler already used, so nothing in the
// scheduler changed. `send`/`ask` also still accept a plain Int, so a program
// written against the pre-Phase-4 integer surface keeps working.
@main def run(): Unit =
  println(Priority.High.toString + " " + Priority.High.ordinal + " " +
    Priority.Medium + " " + Priority.Medium.ordinal + " " +
    Priority.Low + " " + Priority.Low.ordinal + " " + Priority.values.length)
