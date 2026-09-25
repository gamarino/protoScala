// EXPECT: FileNotFoundException
// Point 6 of "protoScala in 10 minutes -- for Python and JavaScript developers".
FileIO.write("shopping.txt", "milk\nbread\n")
println(Source.fromFile("shopping.txt").getLines().mkString(", "))   // milk, bread
println(try Source.fromFile("gone.txt").mkString
        catch case e: IOException => e.getClass)                     // FileNotFoundException
