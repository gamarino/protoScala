// EXPECT-ERROR: may not be by-name
// A member has to hold a value, not a thunk, so a `val`, `var` or case-class
// parameter may not be by-name — scalac reports "`val` parameters may not be
// call-by-name" (D47).
case class Wrong(v: => Int)
println(Wrong(1))
