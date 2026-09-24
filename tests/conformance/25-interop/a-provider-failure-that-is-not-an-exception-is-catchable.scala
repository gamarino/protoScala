// EXPECT: caught: native exception
// The last clause of the boundary shape, which ROADMAP's list has and nothing in
// the engine had: a native from a dlopen'd plug-in that throws something which
// is not a std::exception at all. Without it this escapes the VM and terminates
// the process, which is a crash rather than a Scala exception.
import js.probe as p
@main def run(): Unit =
  try p.boomInt()
  catch case e: RuntimeException => println("caught: " + e.getMessage)
