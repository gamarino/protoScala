// EXPECT: true true None true
// sys.env is the environment as an immutable Map, as in Scala; a variable that is
// not set is absent from it rather than an empty string.
val env = sys.env
println((env("PATH") == System.getenv("PATH")).toString + " " + env.contains("PATH") + " " +
        env.get("PROTOSCALA_SURELY_UNSET_VARIABLE") + " " + (env.size > 1))
