// EXPECT: 3 Person Ada 36 true
case class Person(name: String, age: Int, admin: Boolean)
@main def run(): Unit = {
  val p = Person("Ada", 36, true)
  println(p.productArity.toString + " " + p.productPrefix + " " + p.productElement(0) + " " +
    p.productElement(1) + " " + p.productElement(2))
}
