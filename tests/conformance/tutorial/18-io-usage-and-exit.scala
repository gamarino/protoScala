// EXPECT-ERROR: usage: greet NAME...
@main def greet(args: String*): Unit =
  if args.isEmpty then
    Console.err.println("usage: greet NAME...")
    sys.exit(2)
  val shell = sys.env.getOrElse("SHELL", "an unknown shell")
  for name <- args do println(s"Hello, $name, from $shell")
