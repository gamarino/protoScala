// EXPECT-ERROR: is not a protoScala type
// scalac reports an unknown type differently ("Not found: type Widget"); the
// message here names the extension, because that is what the reader wrote.
extension (x: Widget) def m: Int = 1
@main def run(): Unit = println(1)
