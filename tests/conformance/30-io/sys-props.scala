// EXPECT: true true /
// sys.props answers the JVM's standard system properties that have a meaning
// here: the working directory, the line separator and the file separator.
val p = sys.props
println((p("user.dir").length > 0).toString + " " + (p("line.separator") == "\n") + " " +
        p("file.separator"))
