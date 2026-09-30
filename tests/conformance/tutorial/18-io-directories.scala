// EXPECT: true 10
FileIO.deleteRecursively("project")
FileIO.mkdirs("project/src/main")
FileIO.write("project/src/main/App.scala", "@main def run() = println(1)\n")
FileIO.write("project/README.md", "# project\n")
FileIO.copy("project/README.md", "project/NOTES.md")
FileIO.move("project/NOTES.md", "project/TODO.md")
println(FileIO.list("project"))
println(s"${FileIO.isDirectory("project/src")} ${FileIO.size("project/README.md")}")
