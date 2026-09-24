// EXPECT: probe
// The js. prefix reached a provider loaded by dlopen. The provider is a TEST
// DOUBLE and answers `probe`, never a real library's data.
import js.probe as p
@main def run(): Unit = println(p.name)
