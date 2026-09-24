// EXPECT: probe
// A SELECTOR import of a foreign member: the loader reads one named attribute at
// compile time and binds it to a fresh session global.
import js.probe.{name}
@main def run(): Unit = println(name)
