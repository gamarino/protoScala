// EXPECT-ERROR: no provider registered for 'js'
import js.fs as fs
@main def run(): Unit = println(fs)
