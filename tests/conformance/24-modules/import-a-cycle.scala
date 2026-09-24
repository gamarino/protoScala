// EXPECT-ERROR: cyclic module import
import util.Cycle1
@main def run(): Unit = println(1)
