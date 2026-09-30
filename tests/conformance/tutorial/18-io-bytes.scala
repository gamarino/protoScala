// EXPECT: 6
val png = Bytes(0x89, 0x50, 0x4E, 0x47)
FileIO.writeBytes("header.bin", png)
val back = FileIO.readBytes("header.bin")
println(s"${back.length} bytes, first ${back(0)}, same=${back == png}")
println("héllo".getBytes.length)
