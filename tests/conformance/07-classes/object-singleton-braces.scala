// EXPECT: log=start,init count=2
var log = "start"
object Registry {
  log = log + ",init"
  var count = 0
  def register(): Int = {
    count += 1
    count
  }
}

@main def run(): Unit = {
  Registry.register()
  Registry.register()
  println("log=" + log + " count=" + Registry.count)
}
