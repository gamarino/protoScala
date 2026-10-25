def sign(n: Int): String = if n < 0 then "negative" else "non-negative"

def name(n: Int): String = n match
  case 0 => "zero"
  case 1 | 2 => "small"
  case k if k > 100 => "big"
  case _ => "other"

println(sign(-5))
println(name(2))
