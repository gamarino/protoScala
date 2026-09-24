// EXPECT: [1|2] a-7-b
// This is what closes Phase 3's restriction to `s`, `f` and `raw`: any other
// interpolator is lowered to `StringContext(<literals>).<name>(<args>)`, exactly
// as Scala lowers it, so the interpolator itself is an extension method on
// StringContext and needs no compiler support of its own.
extension (sc: StringContext)
  def bars(args: Any*): String = args.mkString("[", "|", "]")
  def dashed(args: Any*): String =
    var out = ""
    var i = 0
    while i < args.length do
      out = out + sc.parts(i) + "-" + args(i) + "-"
      i += 1
    out + sc.parts(args.length)
@main def run(): Unit =
  println(bars"${1}${2}" + " " + dashed"a${7}b")
