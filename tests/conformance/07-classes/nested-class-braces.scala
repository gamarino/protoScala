// EXPECT-ERROR: must be defined at the top level
@main def run(): Unit = {
  class Local
  println(1)
}
