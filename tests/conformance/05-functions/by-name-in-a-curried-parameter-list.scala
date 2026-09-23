// EXPECT: 2
// A by-name parameter works in any parameter list of a def (D47): the guarded
// branch never runs its argument, so "no" is never printed.
def unless(cond: Boolean)(body: => Int): Int = if cond then 0 else body
println(unless(true)({ println("no"); 1 }))
println(unless(false)({ println("yes"); 2 }))
