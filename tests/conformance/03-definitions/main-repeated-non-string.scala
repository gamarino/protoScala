// EXPECT-ERROR: @main methods take no parameters or a single repeated String parameter
@main def m(args: Int*) = println(args.length)
