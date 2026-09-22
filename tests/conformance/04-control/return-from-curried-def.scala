// EXPECT: 3 5 7
def larger(a: Int)(b: Int): Int =
  if a > b then return a
  b
def pick(a: Int)(b: Int)(c: Int): Int = { if c > 0 then return a + b + c; 0 }
@main def run(): Unit = println(larger(3)(2).toString + " " + larger(1)(5) + " " + pick(1)(2)(4))
