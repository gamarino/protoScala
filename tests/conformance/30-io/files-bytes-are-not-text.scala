// EXPECT: 4 -1 true café
// Bytes that are not UTF-8 survive a round trip untouched (a String could not
// hold them): 0xFF reads back as the signed Byte -1. A String converts to its
// UTF-8 bytes and back: "café" is five bytes, since 'é' takes two.
val tmp = System.getenv("PROTOSCALA_TEST_TMP")
if tmp == "" then throw new IllegalStateException("PROTOSCALA_TEST_TMP is not set")
val path = tmp + "/io-not-text.bin"
FileIO.writeBytes(path, List(0xFF, 0xFE, 0x00, 0x41))
val b = FileIO.readBytes(path)
val cafe = "café".getBytes
println(b.length.toString + " " + b(0) + " " + (cafe.length == 5) + " " + cafe.utf8String)
