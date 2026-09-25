// EXPECT: 7
// `import B1.B2` binds `B2`, not `B1.B2`. This is the case the longest-prefix
// rule gets wrong if it is applied without reservation: `B1.B2` IS in scope (a
// template nested in an object is lifted to a top-level definition with a dotted
// name), so an unrestrained search consumes the whole path and binds the object
// under a name with a dot in it, which no expression can spell. With no selector
// list the last segment is the name being imported, so the search stops one
// segment short.
object B1:
  object B2:
    val q = 7
import B1.B2
@main def run(): Unit = println(B2.q)
