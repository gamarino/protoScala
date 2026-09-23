// EXPECT: 42 true
class Secret(private val code: Int) {
  def same(other: Secret) = code == other.code
}
object Secret {
  def reveal(s: Secret) = s.code
}

@main def run(): Unit = {
  val s = new Secret(42)
  println(Secret.reveal(s).toString + " " + s.same(new Secret(42)))
}
