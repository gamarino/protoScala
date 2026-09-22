// EXPECT: 0 3 1,2,3 6
def count(xs: Int*): Int = xs.length
def all(xs: Int*) = xs
def sum(xs: Int*): Int = { var t = 0; xs.foreach(x => t += x); t }
def forward(xs: Int*): Int = sum(xs*)
@main def run(): Unit =
  println(count().toString + " " + count(1, 2, 3) + " " + all(1, 2, 3).mkString(",") + " " + forward(1, 2, 3))
