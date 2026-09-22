// EXPECT: 55
@main def run(): Unit = {
  var i = 1
  var sum = 0
  while (i <= 10) {
    sum += i
    i += 1
  }
  println(sum)
}
