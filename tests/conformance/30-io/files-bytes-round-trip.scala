// EXPECT: 6 List(0, 1, 127, -128, -1, -1) true Bytes(104, 105)
// FileIO.readBytes / writeBytes carry binary data, which a String cannot: every
// byte 0..255 round-trips. The elements read back as Scala's signed Byte values
// (-128..127), as `Files.readAllBytes` answers them; writing accepts either
// range, so 128 and -128 are the same byte.
val tmp = System.getenv("PROTOSCALA_TEST_TMP")
if tmp == "" then throw new IllegalStateException("PROTOSCALA_TEST_TMP is not set")
val path = tmp + "/io-bytes.bin"
FileIO.writeBytes(path, Bytes(0, 1, 127, 128, 255, -1))
val back = FileIO.readBytes(path)
println(back.length.toString + " " + back.toList + " " +
        (back == Bytes(0, 1, 127, -128, -1, -1)) + " " + "hi".getBytes)
