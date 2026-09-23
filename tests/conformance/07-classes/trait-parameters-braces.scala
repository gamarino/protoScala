// EXPECT: Hello, Ada!
trait Greeter(val greeting: String) {
  def greet(name: String) = greeting + ", " + name + "!"
}
class English extends Greeter("Hello")

@main def run(): Unit = println(new English().greet("Ada"))
