// EXPECT-ERROR: Not found: type java.io.File
// Point 4.
@main def run(): Unit = println(new java.io.File("x"))
