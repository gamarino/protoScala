// EXPECT-ERROR: FileNotFoundException: _data/there-is-no-such-file.txt (No such file or directory)
// The commonest failure of all, and the one a silent answer would hurt most.
// scalac 3.9.0 raises java.io.FileNotFoundException with the message
// "<path> (No such file or directory)"; the class is the same without the
// `java.io.` prefix (D8) and the message is the same in English (D98).
println(Source.fromFile("_data/there-is-no-such-file.txt").mkString)
