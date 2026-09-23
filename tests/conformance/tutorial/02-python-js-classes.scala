// EXPECT: Rex says woof; Tom says meow
trait Animal:
  def name: String
  def sound: String
  def speak = name + " says " + sound

class Dog(val name: String) extends Animal:
  def sound = "woof"

class Cat(val name: String) extends Animal:
  def sound = "meow"

@main def run(): Unit =
  println(new Dog("Rex").speak + "; " + new Cat("Tom").speak)
