// EXPECT-ERROR: ImportError:
// A file that EXISTS and is broken is a failure, not a miss: the message must
// carry the module file and position, never "no module found".
import util.Broken
@main def run(): Unit = println(1)
