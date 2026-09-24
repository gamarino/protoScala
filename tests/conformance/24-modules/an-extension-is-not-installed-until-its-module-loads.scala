// EXPECT-ERROR: squared
// The other side of D82's bargain: session-wide does not mean magic. Nothing in
// this program loads util.Extras, so `squared` is not installed on Int and the
// send fails at run time -- late DETECTION, which the platform accepts, and not
// silent failure, which it does not.
import util.UsesSquared
@main def run(): Unit = println(UsesSquared.viaExtension(3))
