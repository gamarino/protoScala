// EXPECT-ERROR: no provider registered for 'clj'
import clj.string as s
@main def run(): Unit = println(s)
