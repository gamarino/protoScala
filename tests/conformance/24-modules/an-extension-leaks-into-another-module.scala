// EXPECT: 9
// The half a Scala programmer will find surprising, and that D82 promises.
// util/UsesSquared.scala writes `n.squared` and imports NOTHING; it works only
// because util.Extras was loaded earlier in the same session. Under a scoped
// design (E6 ruling A) this would be an error, so this fixture is the one that
// flips.
import util.Extras
import util.UsesSquared
@main def run(): Unit = println(UsesSquared.viaExtension(3))
