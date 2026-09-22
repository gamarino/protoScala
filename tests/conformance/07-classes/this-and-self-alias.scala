// EXPECT: 3 true
class Builder:
  self =>
  var n = 0
  def add(): Builder =
    n += 1
    this
  def me: Builder = self

@main def run(): Unit =
  val b = new Builder
  b.add().add().add()
  println(b.n.toString + " " + (b.me eq b))
