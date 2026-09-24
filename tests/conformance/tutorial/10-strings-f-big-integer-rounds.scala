// EXPECT: 9007199254740993 9007199254740992
@main def run(): Unit =
  val big = 9007199254740993L
  println(f"$big%d $big%.0f")
