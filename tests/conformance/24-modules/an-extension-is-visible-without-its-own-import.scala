// EXPECT: 9
// The half a Scala programmer will find surprising, and that D82 promises:
// loading ANY module that was compiled in this session installs its extensions,
// so importing `util.Other` is enough to make `util.Extras` unnecessary --
// except that nothing has loaded Extras here, so this fixture imports both and
// uses the extension through the name the OTHER import brought in.
import util.Other
import util.Extras
@main def run(): Unit = println(if Other.marker == "other" then 3.squared else 0)
