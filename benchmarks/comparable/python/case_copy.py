# case_copy.py - CPython/protoPython twin of benchmarks/comparable/case_copy.scala:
# 500000 copy-updates of an immutable four-field record, alternating a
# one-field and a two-field update, folding a checksum. Python's idiom for
# Scala's case-class copy is a frozen dataclass and dataclasses.replace.
# Result: see the EXPECT line of case_copy.scala (computed with
# tools/scala3-3.9.0 and confirmed by this twin).
import dataclasses

M = 1000000007


@dataclasses.dataclass(frozen=True, slots=True)
class Account:
    id: int
    balance: int
    tx_count: int
    flags: int


def main():
    a = Account(7, 0, 0, 0)
    check = 0
    i = 0
    while i < 500000:
        if i % 3 == 0:
            a = dataclasses.replace(a, flags=a.flags ^ (i & 7))
        else:
            a = dataclasses.replace(a, balance=a.balance + i, tx_count=a.tx_count + 1)
        check = (check + a.balance % 1000 + a.flags) % M
        i += 1
    print(a.tx_count, a.balance, check)


main()
