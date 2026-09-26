// EXPECT: C() D() true
// An enum may itself be called `Enum`: Scala allows it, and protoScala reported
// "cyclic inheritance: Enum extends itself" because the desugarer named the
// builtin `Enum` marker trait by its source name, which the enum's own generated
// class then shadowed. The marker is now named by its type key, which no source
// can spell. Also covers `case C()`: a case written with an empty parameter
// clause is a zero-parameter case *class*, not a case object, so `Enum.C()`
// finds a companion `apply` -- protoScala raised "value apply is not a member of
// C" before. scalac 3.9 prints `C() D() true`.
enum Enum:
  case C()
  case D()

@main def run(): Unit =
  println(Enum.C().toString + " " + Enum.D() + " " + (Enum.C() == Enum.C()))
