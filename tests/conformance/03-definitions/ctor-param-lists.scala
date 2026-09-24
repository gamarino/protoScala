// EXPECT: 3 3
// Multiple constructor parameter lists CONCATENATE into one flat list (D84), so
// both spellings are the same call. scalac accepts only the curried spelling,
// because its constructor really is curried; the second value on this line is
// protoScala-only and the divergence is what D84 records.
class C(val a: Int)(val b: Int)
@main def run(): Unit =
  val curried = new C(1)(2)
  val flat = new C(1, 2)
  println((curried.a + curried.b).toString + " " + (flat.a + flat.b))
