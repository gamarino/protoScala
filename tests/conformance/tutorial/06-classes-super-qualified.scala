// EXPECT: 2 9
// `super[T].m` names the ancestor explicitly instead of taking the next one in the
// linearization.
trait Greeter:
  def greet: String = "hello"
trait Loud extends Greeter:
  override def greet: String = "HELLO"
class Polite extends Loud:
  override def greet: String = "good day"
  def asLoud: String = super[Loud].greet
@main def run(): Unit =
  val p = new Polite()
  println((if p.asLoud == "HELLO" then 2 else 0).toString + " " +
    (if p.greet == "good day" then 9 else 0))
