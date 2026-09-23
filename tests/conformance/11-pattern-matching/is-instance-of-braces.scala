// EXPECT: true false true true false true true 3
trait Animal
class Dog extends Animal
class Cat extends Animal
@main def run(): Unit = {
  val d: Any = new Dog
  println(d.isInstanceOf[Animal].toString + " " + d.isInstanceOf[Cat] + " " + d.isInstanceOf[Dog] + " " +
    "s".isInstanceOf[String] + " " + null.isInstanceOf[String] + " " + (1, 2).isInstanceOf[Product] + " " +
    3L.isInstanceOf[Int] + " " + (if d.asInstanceOf[Animal] eq d then 3 else 0))
}
