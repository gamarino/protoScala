// EXPECT-ERROR: before
// sys.exit(3) ends the program at once with status 3: the runner sees a
// non-zero exit, the output written before it is kept, and nothing after it
// runs. tests/cli/io-program.sh checks the exact status.
println("before")
sys.exit(3)
println("after")
