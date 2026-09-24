// EXPECT: 1 2 | 5 6
// A default may read the parameters declared before it, and an enclosing local.
def span(from: Int, to: Int = from + 1): String = from.toString + " " + to
@main def run(): Unit = println(span(1) + " | " + span(5))
