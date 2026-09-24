// EXPECT: inner outer
@main def run(): Unit = {
  try {
    try {
      throw new RuntimeException("a")
    } catch {
      case e: RuntimeException => {
        print("inner ")
        throw new IllegalStateException("b")
      }
    }
  } catch {
    case e: IllegalStateException => println("outer")
  }
}
