// EXPECT-ERROR: no module found for 'util.Nope' (tried
// A miss names every path it tried, in the order it tried them.
import util.Nope
@main def run(): Unit = println(1)
