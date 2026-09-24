// EXPECT-ERROR: no module found for 'util.Nope'
import util.Nope
@main def run(): Unit = println(1)
