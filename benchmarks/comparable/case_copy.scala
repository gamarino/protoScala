// EXPECT: 333333 83333166667 248001884
// case_copy.scala - 500000 copy-updates of an immutable four-field case class,
// alternating a one-field and a two-field update, and a checksum folded from
// every version. The paradigm's own write: a new value, never a mutation.
// Twin of benchmarks/comparable/python/case_copy.py (a frozen dataclass and
// dataclasses.replace). The three numbers were computed with
// tools/scala3-3.9.0 and confirmed by the CPython twin.

case class Account(id: Int, balance: Long, txCount: Int, flags: Int)

@main def benchCaseCopy(): Unit =
  var a = Account(7, 0L, 0, 0)
  var check = 0L
  var i = 0
  while i < 500000 do
    a = if i % 3 == 0 then a.copy(flags = a.flags ^ (i & 7))
        else a.copy(balance = a.balance + i, txCount = a.txCount + 1)
    check = (check + a.balance % 1000 + a.flags) % 1000000007L
    i += 1
  println(s"${a.txCount} ${a.balance} $check")
