// EXPECT: finally 7
def f(): Int = {
  try {
    return 7
  } finally {
    print("finally ")
  }
}
@main def run(): Unit = println(f())
