// EXPECT: 3
/* outer /* inner */ still a comment */
@main def run(): Unit =
  // a line comment
  println(1 + /* inline */ 2)
