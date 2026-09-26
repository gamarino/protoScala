// EXPECT: C() D() true
// The brace-syntax twin of enum-named-enum.scala.
enum Enum { case C(); case D() }

@main def run(): Unit = {
  println(Enum.C().toString + " " + Enum.D() + " " + (Enum.C() == Enum.C()))
}
