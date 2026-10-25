def greet(name: String, greeting: String = "Hello"): String = s"$greeting, $name"

def area(w: Int): Int = w * w
def area(w: Int, h: Int): Int = w * h

println(greet("Ada"))
println(greet("Ada", greeting = "Hi"))
println(area(3) + area(3, 4))
