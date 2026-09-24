// EXPECT: ab ab bc aa b ac Abc 1 true
// Verified against tools/scala3-3.9.0.
@main def run(): Unit =
  println("abc".init + " " + "abc".take(2) + " " + "abc".drop(1) + " " +
    "aab".takeWhile(_ == 'a') + " " + "aab".dropWhile(_ == 'a') + " " +
    "abc".filter(_ != 'b') + " " + "abc".capitalize + " " + "ab".lastIndexOf("b") + " " +
    "true".toBoolean)
