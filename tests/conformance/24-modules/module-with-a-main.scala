// EXPECT-ERROR: a module may not define an @main method
import util.WithMain
@main def run(): Unit = println(1)
