// EXPECT: inner outer done
def f(): Int = {
  try {
    try {
      return 1
    } finally {
      print("inner ")
    }
  } finally {
    print("outer ")
  }
}
@main def run(): Unit = {
  f()
  println("done")
}
