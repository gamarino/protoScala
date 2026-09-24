// EXPECT-ERROR: cyclic module import
// util.Cycle1 imports util.Cycle2, which imports util.Cycle1 back.
import util.Cycle1
@main def run(): Unit = println(1)
