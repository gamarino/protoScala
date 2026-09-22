// EXPECT: negative zero small big:1000
def classify(n: Int): String = n match
  case 0 => "zero"
  case x if x < 0 => "negative"
  case x if x < 10 => "small"
  case big => "big:" + big

@main def run(): Unit = println(List(-5, 0, 7, 1000).map(classify).mkString(" "))
