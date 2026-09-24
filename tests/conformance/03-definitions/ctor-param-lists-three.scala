// EXPECT: 6
class D(val a: Int)(val b: Int)(val c: Int)
@main def run(): Unit = println(new D(1)(2)(3).a + new D(1)(2)(3).b + new D(1)(2)(3).c)
