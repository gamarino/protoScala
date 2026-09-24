// EXPECT: RuntimeException SomeUnknownError: for the test
// A native failure whose class name is not one the prelude defines must still be
// catchable, as a RuntimeException that keeps the original name in its message,
// so nothing is ever swallowed. Every class name the runtime actually raises IS
// in the prelude, so this fixture reaches the fallback through `__raise`, the
// internal primitive that raises an arbitrary class name.
@main def run(): Unit =
  try __raise("SomeUnknownError", "for the test")
  catch case e: RuntimeException => println(e.getClass + " " + e.getMessage)
