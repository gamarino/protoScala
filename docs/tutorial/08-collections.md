# 8. Collections

> **Implementation status.** Everything in this chapter runs today: `List`,
> `Vector`, `Range`, `Map`, `Set`, `Option`, `Either`, `Try` and tuples, with
> the method surface listed in each section. What is not implemented: `Seq` and
> `Iterable` as traits (§8.10, D65), `collect` (D63), `Ordering` and therefore
> `sorted` on your own types (D62), `SortedMap`/`ListMap`, and `Array`. A
> mutable collection library is not planned: protoCore's structures are
> immutable by construction (DESIGN §1.1).

Chapter 7 used `List` without explaining it. This chapter is the explanation,
and it covers the other seven types you will reach for. They have one property
in common, and everything else follows from it: **a collection is never
modified.** `xs :+ 4` does not append to `xs`; it answers a new list that has
one more element, and `xs` is exactly what it was. The same holds for
`m + (k -> v)` on a `Map` and `s + e` on a `Set`.

That sounds expensive and is not. Under each of these types is a protoCore
persistent structure, so the old collection and the new one **share almost all
of their internal nodes**; building the new one copies a path, not the data.
It is the reason Scala programs pass collections between threads without
locking or copying them: a value nobody can change is safe everywhere.

## 8.1 `List`

Fixture: [`tests/conformance/tutorial/08-collections-list-basics.scala`](../../tests/conformance/tutorial/08-collections-list-basics.scala)

```scala
@main def run(): Unit =
  val xs = List(1, 2, 3, 4, 5)
  println(xs.head.toString + " " + xs.tail + " " + xs.length + " " + xs.isEmpty + " " + xs(2))
```

Prints:

```text
1 List(2, 3, 4, 5) 5 false 3
```

`List(1, 2, 3, 4, 5)` builds a list; `head` is its first element, `tail` is
everything after the first, `length` counts, and `xs(2)` reads by index
(0-based). `head` and `last` on an empty list raise
`NoSuchElementException: head of empty list`, so `headOption` and `lastOption`
are there for the cases where empty is a normal answer. The empty list is
`Nil`, and it prints as `List()`.

Building a longer list from a shorter one:

Fixture: [`tests/conformance/tutorial/08-collections-list-building.scala`](../../tests/conformance/tutorial/08-collections-list-building.scala)

```scala
@main def run(): Unit =
  val xs = List(2, 3)
  val longer = 1 :: xs
  println(longer.toString + " " + xs + " " + (xs :+ 4) + " " + (xs ++ List(9)))
```

Prints:

```text
List(1, 2, 3) List(2, 3) List(2, 3, 4) List(2, 3, 9)
```

`1 :: xs` prepends, `xs :+ 4` appends, `0 +: xs` prepends (the mirror of `:+`),
and `xs ++ ys` concatenates. The second field is the point of the program:
after three operations that all "changed" `xs`, `xs` still prints
`List(2, 3)`. `::` also reads backwards for a reason — an operator ending in
`:` is right-associative, so `1 :: 2 :: Nil` groups as `1 :: (2 :: Nil)`.

The three methods you will use most:

Fixture: [`tests/conformance/tutorial/08-collections-list-transforms.scala`](../../tests/conformance/tutorial/08-collections-list-transforms.scala)

```scala
@main def run(): Unit =
  val xs = List(1, 2, 3, 4, 5)
  println(xs.map(_ * 2).toString + " " + xs.filter(_ % 2 == 1) + " " +
    xs.foldLeft(0)(_ + _) + " " + xs.sum + " " + xs.mkString(", "))
```

Prints:

```text
List(2, 4, 6, 8, 10) List(1, 3, 5) 15 15 1, 2, 3, 4, 5
```

- `map(f)` answers a list of the same length with `f` applied to each element.
- `filter(p)` answers the elements for which `p` holds; `filterNot(p)` the rest.
- `foldLeft(z)(op)` walks left to right carrying an accumulator:
  `foldLeft(0)(_ + _)` is `((((0+1)+2)+3)+4)+5`. `foldRight` walks the other
  way, calling `op(element, accumulator)`.
- `sum`, `product`, `min`, `max` and `reduce` are the common folds by name.
  `min`, `max` and `reduce` on an empty list fail loudly
  (`UnsupportedOperationException: min of empty collection`) rather than
  inventing a zero; `sum` and `product` answer the identity, `0` and `1`.
- `mkString` renders, with one separator or with a prefix and suffix
  (`mkString("[", ", ", "]")`).

Asking questions about a list:

Fixture: [`tests/conformance/tutorial/08-collections-list-searching.scala`](../../tests/conformance/tutorial/08-collections-list-searching.scala)

```scala
@main def run(): Unit =
  val xs = List(1, 2, 3, 4, 5)
  println(xs.contains(3).toString + " " + xs.exists(_ > 4) + " " + xs.forall(_ > 0) + " " +
    xs.find(_ > 3) + " " + xs.count(_ % 2 == 0) + " " + xs.indexOf(2) + " " +
    xs.partition(_ % 2 == 0))
```

Prints:

```text
true true true Some(4) 2 1 (List(2, 4),List(1, 3, 5))
```

`find` returns an `Option` (§8.6) because there may be no such element —
compare that with an index-based search, which has to invent `-1` the way
`indexOf` does. `partition(p)` splits in one pass and answers the pair
`(matching, rest)`, which prints with no space after the comma because it is a
`Tuple2` (chapter 7, §7.2).

Ordering and grouping:

Fixture: [`tests/conformance/tutorial/08-collections-list-sorting-and-grouping.scala`](../../tests/conformance/tutorial/08-collections-list-sorting-and-grouping.scala)

```scala
@main def run(): Unit =
  val words = List("pear", "fig", "apple")
  println(words.sorted.toString + " " + words.sortBy(_.length) + " " +
    words.sortWith(_ > _) + " " + words.groupBy(_.length).toList.sortBy(_._1))
```

Prints:

```text
List(apple, fig, pear) List(fig, pear, apple) List(pear, fig, apple) List((3,List(fig)), (4,List(pear)), (5,List(apple)))
```

`sorted` orders numbers and strings by their natural order; `sortBy(f)` orders
by a computed key; `sortWith(lt)` orders by a comparison you write. All three
are **stable**: elements that compare equal keep their input order.
`groupBy(f)` answers a `Map` from key to the elements that produced it, each
group in the elements' own order — and, because a `Map` has no iteration order
(§8.4), the example sorts before printing.

`sorted` on values the runtime cannot order is an error rather than an
arbitrary order; §8.10 (D62) has the program.

The whole `List` surface, as delivered:

| Group | Methods |
|---|---|
| Access | `head`, `tail`, `last`, `init`, `apply(i)`, `headOption`, `lastOption`, `length`, `size`, `isEmpty`, `nonEmpty` |
| Building | `::`, `+:`, `:+`, `++`, `updated(i, v)`, `reverse`, `distinct`, `flatten` |
| Transform | `map`, `flatMap`, `filter`, `filterNot`, `foreach`, `zip`, `zipWithIndex` |
| Slice | `take`, `drop`, `takeWhile`, `dropWhile`, `splitAt`, `partition` |
| Fold | `foldLeft`, `foldRight`, `reduce`, `reduceLeft`, `reduceRight`, `sum`, `product`, `min`, `max`, `minBy`, `maxBy` |
| Query | `contains`, `exists`, `forall`, `find`, `count`, `indexOf` |
| Order | `sorted`, `sortBy`, `sortWith`, `groupBy` |
| Render | `mkString`, `mkString(sep)`, `mkString(pre, sep, post)`, `toString` |
| Convert | `toList`, `toVector`, `toSet`, `toMap`, `toSeq`, `iterator` |

**For Python and JavaScript readers.** A Scala `List` is **not** a Python
`list` or a JavaScript `Array`: there is no `append`, no `xs[0] = v`, no
`sort()` that reorders in place. Every one of those has an immutable
counterpart that answers a new list — `xs :+ v`, `xs.updated(0, v)`,
`xs.sorted`. `map`, `filter` and `foldLeft` are JavaScript's `map`, `filter`
and `reduce`, and Python's comprehensions and `functools.reduce`. If you want
the indexed container you are used to, the closer name is `Vector` (§8.2).

## 8.2 `Vector`

Fixture: [`tests/conformance/tutorial/08-collections-vector-basics.scala`](../../tests/conformance/tutorial/08-collections-vector-basics.scala)

```scala
@main def run(): Unit =
  val v = Vector(10, 20, 30)
  println(v.toString + " " + v(1) + " " + (v :+ 40) + " " + v.updated(0, 0) + " " +
    v.map(_ / 10) + " " + v.toList)
```

Prints:

```text
Vector(10, 20, 30) 20 Vector(10, 20, 30, 40) Vector(0, 20, 30) Vector(1, 2, 3) List(10, 20, 30)
```

A `Vector` has the same surface as a `List` — every method in the table above
works on both — and answers a `Vector` where `List` answers a `List`. It is
the type to write when the code's subject is an *indexed* sequence: a row of
pixels, a board, a buffer of samples.

**Where the JVM's advice does not apply.** On the JVM you choose between them
on cost: a `List` is a chain of cons cells (O(1) prepend, O(n) index), a
`Vector` is a wide trie (effectively constant index and append). In protoScala
both wrap **one** protoCore persistent list, and one implementation serves
both prototypes, so indexing a `List` and indexing a `Vector` cost the same,
and so do prepend and append. Choose the one that documents your intent; if
you later run the program on the JVM, the choice will be the right one there
too.

A `List` and a `Vector` with the same elements are equal, and hash alike:

Fixture: [`tests/conformance/tutorial/08-collections-seq-equality.scala`](../../tests/conformance/tutorial/08-collections-seq-equality.scala)

```scala
@main def run(): Unit =
  println((List(1, 2, 3) == Vector(1, 2, 3)).toString + " " + ((1 to 3) == List(1, 2, 3)) + " " +
    (List(1, 2).## == Vector(1, 2).##) + " " + Map(Vector(1, 2) -> "hit").getOrElse(List(1, 2), "miss"))
```

Prints:

```text
true true true hit
```

That is Scala's rule, and it extends to `Range`: three kinds of sequence, one
notion of equality, decided element by element. The last field is the
consequence that matters in practice — a `Map` keyed by a `Vector` is found by
the equal `List`, because equal values must hash alike.

## 8.3 `Range`

Fixture: [`tests/conformance/tutorial/08-collections-range-basics.scala`](../../tests/conformance/tutorial/08-collections-range-basics.scala)

```scala
@main def run(): Unit =
  println((0 until 5).toString + " " + (0 until 5).toList + " " + (1 to 5).toList + " " +
    (0 until 10 by 3).toList + " " + (0 until 5).length + " " + (1 to 100).sum)
```

Prints:

```text
Range 0 until 5 List(0, 1, 2, 3, 4) List(1, 2, 3, 4, 5) List(0, 3, 6, 9) 5 5050
```

`0 until 5` excludes its upper bound, `1 to 5` includes it, and `by` sets the
step (which may be negative, and may not be zero). A `Range` holds three
numbers, not its elements: `length`, `apply`, `head`, `last` and `sum` are
arithmetic, so `(0 until 1000000000).sum` answers at once and allocates
nothing. `toList` is what materialises it.

The idiomatic counted loop is a `Range` in a `for`:

Fixture: [`tests/conformance/tutorial/08-collections-range-for.scala`](../../tests/conformance/tutorial/08-collections-range-for.scala)

```scala
@main def run(): Unit =
  var total = 0
  for i <- 0 until 5 do
    total += i
  val squares = for i <- 1 to 4 yield i * i
  println(total.toString + " " + squares + " " + (1 to 3).map(_ * 2))
```

Prints:

```text
10 List(1, 4, 9, 16) List(2, 4, 6)
```

`for i <- 0 until n do …` is the loop you would write as `for i in range(n)` or
`for (let i = 0; i < n; i++)`. With `yield` it is a comprehension instead of a
loop, and it collects the results — into a `List` here, where Scala answers an
`IndexedSeq` (§8.10, D68). `map` and `filter` on a `Range` answer a `List` for
the same reason. Chapter 9 covers comprehensions in full.

## 8.4 `Map`

Fixture: [`tests/conformance/tutorial/08-collections-map-basics.scala`](../../tests/conformance/tutorial/08-collections-map-basics.scala)

```scala
@main def run(): Unit =
  val ages = Map("Ada" -> 36, "Alan" -> 41)
  println(ages("Ada").toString + " " + ages.get("Ada") + " " + ages.get("Grace") + " " +
    ages.getOrElse("Grace", 0) + " " + ages.contains("Alan") + " " + ages.size)
```

Prints:

```text
36 Some(36) None 0 true 2
```

`"Ada" -> 36` is a pair: `->` is an ordinary method on any value that builds a
`Tuple2`, so `Map(a -> b, c -> d)` is `Map((a, b), (c, d))` written the way it
reads. There are three ways to look a key up, and the difference is what they
do when it is absent: `ages(k)` fails, `ages.get(k)` answers an `Option`, and
`ages.getOrElse(k, d)` answers `d`.

Failing means failing:

Fixture: [`tests/conformance/tutorial/08-collections-map-missing-key.scala`](../../tests/conformance/tutorial/08-collections-map-missing-key.scala)

```scala
@main def run(): Unit =
  println(Map("Ada" -> 36)("Grace"))
```

It stops with:

```text
08-collections-map-missing-key.scala:3: error: NoSuchElementException: key not found: Grace
```

A map is immutable, like everything else here:

Fixture: [`tests/conformance/tutorial/08-collections-map-is-immutable.scala`](../../tests/conformance/tutorial/08-collections-map-is-immutable.scala)

```scala
@main def run(): Unit =
  val stock = Map("apples" -> 3, "pears" -> 0)
  val restocked = stock + ("pears" -> 12)
  println(restocked.toList.sortBy(_._1))
```

Prints:

```text
List((apples,3), (pears,12))
```

`stock + ("pears" -> 12)` answers a new map in which `"pears"` is 12; `stock`
still has it at 0. The two maps share all the structure they have in common,
so this is a cheap operation and not a copy of the map.

Fixture: [`tests/conformance/tutorial/08-collections-map-updates.scala`](../../tests/conformance/tutorial/08-collections-map-updates.scala)

```scala
@main def run(): Unit =
  val counts = Map("a" -> 1)
  val more = counts + ("b" -> 2)
  println(counts.size.toString + " " + more.size + " " + (more - "a").toList + " " +
    more.updated("a", 9).toList.sortBy(_._1) + " " + (counts ++ Map("c" -> 3)).toList.sortBy(_._1))
```

Prints:

```text
1 2 List((b,2)) List((a,9), (b,2)) List((a,1), (c,3))
```

`+` adds or replaces an entry, `-` removes a key, `updated(k, v)` is `+` spelled
as a method, and `++` merges another map (its entries win). Each answers a new
map.

**Iteration order is not defined.** A `Map` and a `Set` iterate in ascending
order of their internal hashes, which is neither insertion order nor the
keys' own order, and is not promised to stay the same between releases. Scala
promises nothing either, so the rule for both is the same: if the order of the
output matters, sort. Every example in this tutorial does.

Fixture: [`tests/conformance/tutorial/08-collections-map-iteration-sorts.scala`](../../tests/conformance/tutorial/08-collections-map-iteration-sorts.scala)

```scala
@main def run(): Unit =
  val ages = Map("Grace" -> 30, "Ada" -> 36, "Alan" -> 41)
  println(ages.keys.toList.sorted.toString + " " + ages.values.toList.sorted + " " +
    ages.toList.sortBy(_._1))
```

Prints:

```text
List(Ada, Alan, Grace) List(30, 36, 41) List((Ada,36), (Alan,41), (Grace,30))
```

`keys`, `values` and `toList` are the three ways out of a map; `toList` gives
the pairs, so `sortBy(_._1)` orders by key and `sortBy(_._2)` by value.

A function over a map receives each entry, and may take it either as one pair
or as two arguments:

Fixture: [`tests/conformance/tutorial/08-collections-map-pair-functions.scala`](../../tests/conformance/tutorial/08-collections-map-pair-functions.scala)

```scala
@main def run(): Unit =
  val ages = Map("Ada" -> 36, "Alan" -> 41)
  println(ages.map((name, age) => (name, age + 1)).toList.sortBy(_._1).toString + " " +
    ages.filter((name, age) => age > 40).toList + " " +
    ages.map(pair => pair._1).toList.sorted)
```

Prints:

```text
List((Ada,37), (Alan,42)) List((Alan,41)) List(Ada, Alan)
```

`map` on a map answers a map when the function answers pairs, and the
function's own arity decides how it is called — `(name, age) => …` gets the
key and the value, `pair => …` gets the `Tuple2`. Scala 3 reaches the same
place through parameter untupling.

**What a key must be.** A key is compared the way `==` compares it: numbers,
strings, `Char`s, `Boolean`s, tuples, case classes, `List`s and `Vector`s are
compared by their contents, so a freshly built equal key finds the entry. An
instance of a plain class that does not override `equals` is compared by
identity, so two structurally identical instances are two different keys —
exactly as on the JVM. §8.10 (D71) has the one case where a class that
overrides `equals` carelessly behaves differently from Scala's.

| Group | Methods |
|---|---|
| Look up | `apply(k)`, `get`, `getOrElse`, `contains`, `size`, `isEmpty`, `nonEmpty` |
| Update | `+`, `-`, `updated(k, v)`, `++` |
| Traverse | `keys`, `values`, `keySet`, `head`, `foreach`, `map`, `flatMap`, `filter`, `forall`, `exists`, `count` |
| Convert | `toList`, `toSeq`, `toSet`, `mkString` |

## 8.5 `Set`

Fixture: [`tests/conformance/tutorial/08-collections-set-basics.scala`](../../tests/conformance/tutorial/08-collections-set-basics.scala)

```scala
@main def run(): Unit =
  val s = Set(1, 2, 3, 2, 1)
  println(s.size.toString + " " + s.contains(2) + " " + s(9) + " " + (s + 4).size + " " +
    (s - 1).toList.sorted + " " + s.toList.sorted)
```

Prints:

```text
3 true false 4 List(2, 3) List(1, 2, 3)
```

A `Set` holds each element once — the five arguments become three elements.
`s(9)` is `s.contains(9)`: a set *is* its membership test, which is the
universal `apply` rule (chapter 6, §6.8) put to good use. `+` and `-` answer
new sets, and elements are classified exactly as `Map` keys are (§8.4).

Fixture: [`tests/conformance/tutorial/08-collections-set-algebra.scala`](../../tests/conformance/tutorial/08-collections-set-algebra.scala)

```scala
@main def run(): Unit =
  val a = Set(1, 2, 3)
  val b = Set(3, 4)
  println((a union b).toList.sorted.toString + " " + (a intersect b).toList.sorted + " " +
    (a diff b).toList.sorted + " " + Set(1, 2).subsetOf(a))
```

Prints:

```text
List(1, 2, 3, 4) List(3) List(1, 2) true
```

`union`, `intersect` and `diff` are the set operations, written infix because
any one-argument method can be (`a union b` is `a.union(b)`). `++` and `--` are
`union` and `diff` under their operator names. As always, the output is sorted
before printing, because a `Set` has no order.

## 8.6 `Option`: a collection of at most one

Chapter 7, §7.3 introduced `Option` as "a value that may be missing". The other
half of the idea is that it is a **collection with zero or one element**, and
that is why it carries the collection methods:

Fixture: [`tests/conformance/tutorial/08-collections-option-as-a-collection.scala`](../../tests/conformance/tutorial/08-collections-option-as-a-collection.scala)

```scala
@main def run(): Unit =
  val found: Option[Int] = Some(3)
  val missing: Option[Int] = None
  println(found.toList.toString + " " + missing.toList + " " + found.map(_ * 2) + " " +
    missing.map(_ * 2) + " " + found.filter(_ > 5) + " " + found.exists(_ > 2) + " " +
    missing.getOrElse(0))
```

Prints:

```text
List(3) List() Some(6) None None true 0
```

Read the `None` column: `map` on an empty collection is empty, `filter` that
removes the only element is empty, and nothing has to test for absence. That
is the whole trick — instead of `if (x != null) f(x) else null`, you write
`o.map(f)` and the empty case takes care of itself.

Because `Option` has `map` and `flatMap`, it composes with the other
collections in a comprehension:

Fixture: [`tests/conformance/tutorial/08-collections-option-in-a-for.scala`](../../tests/conformance/tutorial/08-collections-option-in-a-for.scala)

```scala
@main def run(): Unit =
  val ages = Map("Ada" -> 36)
  val names = List("Ada", "Grace")
  val known = for
    name <- names
    age <- ages.get(name)
  yield name + ":" + age
  println(known.toString + " " + List(Some(1), None, Some(3)).flatMap(o => o.toList))
```

Prints:

```text
List(Ada:36) List(1, 3)
```

`"Grace"` is not in the map, so `ages.get(name)` is `None`, so that iteration
contributes nothing — a lookup and a filter in one line, with no `contains`
call and no `if`. The second field is the same idea by hand: `flatMap` over
lists of `Option`s drops the empty ones.

`Option`'s surface: `isEmpty`, `isDefined`, `nonEmpty`, `get`, `getOrElse`,
`orElse`, `orNull`, `map`, `flatMap`, `filter`, `withFilter`, `foreach`,
`contains`, `exists`, `forall`, `count`, `fold`, `zip`, `toList`, `toSeq`,
`iterator`, `toRight`, `toLeft`.

## 8.7 `Either` and `Try`

`Option` says that a value is missing. `Either` says *why*.

Fixture: [`tests/conformance/tutorial/08-collections-either.scala`](../../tests/conformance/tutorial/08-collections-either.scala)

```scala
def parse(s: String): Either[String, Int] =
  if s == "42" then Right(42) else Left("not a number: " + s)

@main def run(): Unit =
  println(parse("42").toString + " " + parse("x") + " " + parse("42").map(_ + 1) + " " +
    parse("x").map(_ + 1) + " " + parse("x").getOrElse(0) + " " +
    parse("x").fold(e => "err " + e, n => "ok " + n) + " " + parse("x").toOption)
```

Prints:

```text
Right(42) Left(not a number: x) Right(43) Left(not a number: x) 0 err not a number: x None
```

`Either[L, R]` has two cases: `Left(l)` carries the failure, `Right(r)` the
result. It is **right-biased**, which means `map`, `flatMap`, `foreach`,
`exists` and `forall` work on the `Right` and pass a `Left` through untouched —
so a chain of steps stops at the first failure and keeps its message. `fold`
handles both sides at once, `swap` exchanges them, and `toOption` throws the
message away when you no longer need it.

`Try` is the same shape for a computation that may *fail* rather than return a
failure:

Fixture: [`tests/conformance/tutorial/08-collections-try.scala`](../../tests/conformance/tutorial/08-collections-try.scala)

```scala
@main def run(): Unit =
  val ok = Try {
    List(1, 2, 3)(1)
  }
  val bad = Try {
    List(1, 2, 3)(9)
  }
  println(ok.toString + " " + bad.isFailure + " " + bad.getOrElse(-1) + " " +
    ok.map(_ * 10) + " " + bad.recover(e => 0) + " " + bad.toEither.isLeft)
```

Prints:

```text
Success(2) true -1 Success(20) Success(0) true
```

`Try { … }` runs the block and answers `Success(value)` or `Failure(error)`.
The block is a **by-name** parameter, so the failure is caught inside `Try` and
not at the call — `Try { … }` and `Try(expr)` both work, exactly as in Scala.
`recover(f)` turns a failure back into a value, `recoverWith(f)` into another
`Try`, `orElse` supplies an alternative, and `toEither`/`toOption` convert.
Until `try`/`catch` and exception values arrive in Phase 4, `Try` is how a
protoScala program handles a failure it expects.

`Either`: `isLeft`, `isRight`, `map`, `flatMap`, `foreach`, `exists`,
`forall`, `getOrElse`, `fold`, `swap`, `toOption`.
`Try`: `isSuccess`, `isFailure`, `get`, `getOrElse`, `orElse`, `map`,
`flatMap`, `foreach`, `recover`, `recoverWith`, `toOption`, `toEither`.

## 8.8 Conversions

Fixture: [`tests/conformance/tutorial/08-collections-conversions.scala`](../../tests/conformance/tutorial/08-collections-conversions.scala)

```scala
@main def run(): Unit =
  val xs = List(1, 2, 2, 3)
  println(xs.toVector.toString + " " + xs.toSet.toList.sorted + " " + Vector(1, 2).toList + " " +
    (0 until 3).toVector + " " + List(("a", 1), ("b", 2)).toMap.toList.sortBy(_._1) + " " +
    Map("a" -> 1).toList + " " + Set(1).toList)
```

Prints:

```text
Vector(1, 2, 2, 3) List(1, 2, 3) List(1, 2) Vector(0, 1, 2) List((a,1), (b,2)) List((a,1)) List(1)
```

| From | To | Method |
|---|---|---|
| any sequence | `List` | `toList` |
| any sequence | `Vector` | `toVector` |
| any sequence | `Set` | `toSet` (duplicates collapse) |
| a sequence of pairs | `Map` | `toMap` (a later key wins) |
| `Map` | list of pairs | `toList`, `toSeq` |
| `Map` | `Set` of pairs | `toSet` |
| `Set` | `List` | `toList` |
| `Option` | `List` | `toList` |
| `Either` | `Option` | `toOption` |
| `Try` | `Option`, `Either` | `toOption`, `toEither` |
| `String` | `List[Char]` | `toList` (chapter 10) |

Conversions are the seam between the collection you have and the collection an
interface asks for; since nothing is ever modified in place, converting is
always safe.

## 8.9 For Python and JavaScript developers

There is one big idea and then a table. The big idea: **none of these can be
modified.** There is no `append`, no `push`, no `del`, no `d[k] = v`. Every
operation that would change a collection instead answers a new one, and the
one you had is still valid. Programs come out shorter than you expect, because
nothing has to defend itself against a collection changing under it.

| Python | JavaScript | protoScala |
|---|---|---|
| `list` | `Array` | `Vector` for indexed access, `List` for a sequence |
| `dict` | `Map` / object | `Map` |
| `set` | `Set` | `Set` |
| `tuple` | a fixed array | a tuple, `(a, b)` (chapter 7) |
| `range(n)` | — | `0 until n` |
| `xs[i]` | `xs[i]` | `xs(i)` |
| `len(xs)` | `xs.length` | `xs.length` |
| `[f(x) for x in xs]` | `xs.map(f)` | `xs.map(f)` |
| `[x for x in xs if p(x)]` | `xs.filter(p)` | `xs.filter(p)` |
| `functools.reduce(op, xs, z)` | `xs.reduce(op, z)` | `xs.foldLeft(z)(op)` |
| `x in xs` | `xs.includes(x)` | `xs.contains(x)` |
| `d.get(k, default)` | `m.get(k) ?? default` | `m.getOrElse(k, default)` |
| `k in d` | `m.has(k)` | `m.contains(k)` |
| `d[k] = v` | `m.set(k, v)` | `val m2 = m + (k -> v)` |
| `del d[k]` | `m.delete(k)` | `val m2 = m - k` |
| `xs.append(v)` | `xs.push(v)` | `val ys = xs :+ v` |
| `sorted(xs)` | `[...xs].sort()` | `xs.sorted` |
| `sorted(xs, key=f)` | `xs.slice().sort(cmp)` | `xs.sortBy(f)` |
| `", ".join(xs)` | `xs.join(", ")` | `xs.mkString(", ")` |
| `itertools.groupby` | manual | `xs.groupBy(f)` |
| `d.items()` | `m.entries()` | `m.toList` |
| `d.keys()`, `d.values()` | `m.keys()`, `m.values()` | `m.keys`, `m.values` |

Three habits to unlearn:

1. **`for k, v in d.items()`.** Here you write
   `m.toList.sortBy(_._1).foreach(pair => …)`, or `m.foreach((k, v) => …)`. If
   the order of the output matters, the sort is not optional: a `Map` has no
   order to rely on (§8.4).
2. **`None` and `undefined`.** A missing map entry is `None`, a value of type
   `Option`, and you handle it with `map`/`getOrElse` rather than by testing
   (§8.6). `m(k)` on an absent key is an error, not `undefined`.
3. **Sorting in place.** `xs.sorted` answers a sorted list; it does not sort
   `xs`. There is nothing to copy first and no `sort()` to accidentally share.

Python's dictionaries preserve insertion order and its sets do not; here
neither does. Where you relied on insertion order, sort explicitly on the field
you actually care about — it says what you meant, and it keeps working.

## 8.10 What differs from Scala 3 in this area

First, what does **not** differ, since it is what a Scala programmer will check
first: `Map` and `Set` keys are classified and compared exactly as Scala
compares them (a case class, tuple, `List`, `String`, `Char` or number by
value; a plain class by identity, with cooperative numeric equality so `1`,
`1L` and `1.0` are one key), sequence equality and hashing agree across
`List`, `Vector` and `Range` so `List(1,2) == Vector(1,2) == (1 to 2)`, and
`Try { … }` takes its block by name, so a failure inside the block is caught by
the `Try`.

Now the departures.

**D58 — `Map` and `Set` iteration order is ascending-hash and unspecified.**
It is neither insertion order nor the keys' order, and it may change between
releases. Scala guarantees no order either, so nothing is being contradicted;
what differs is that a JVM program that happened to rely on `Map1`…`Map4`'s
insertion order will see a different order here. Every fixture in the suite
sorts before printing, which is the practice this chapter recommends. An
ordered map is a different type (`ListMap`, `SortedMap`) and is not
implemented.

**D59 — `Vector.hashCode` equals `List.hashCode`, and both differ from the
JVM's.** Sequence hashing is computed in one place for `List`, `Vector` and
`Range`, so equal sequences of different kinds hash alike, which is the
property `==` requires and the reason a `Map` keyed by a `Vector` is found by
an equal `List` (§8.2). The *value* of the hash is not Scala's MurmurHash3
sequence hash, so a hash written into a file by a JVM program will not match.
Case classes, tuples, strings and numbers **are** bit-identical to the JVM's
(chapter 3).

**D61 — a `Range` bound must fit a 54-bit integer.**

Fixture: [`tests/conformance/tutorial/08-collections-range-bound-must-be-small.scala`](../../tests/conformance/tutorial/08-collections-range-bound-must-be-small.scala)

```scala
@main def run(): Unit =
  val bound = 9007199254740991L + 1
  println((0 until bound).length)
```

It stops with:

```text
08-collections-range-bound-must-be-small.scala:4: error: IllegalArgumentException: a Range bound must fit a 54-bit integer
```

A `Range` keeps its three numbers as tagged small integers, which is what makes
every `Range` method allocation-free. Scala's `Range` is limited to `Int`
bounds, so no Scala program is affected; a protoScala program that wants more
than 2^53 elements must count with a `while` loop.

**D62 — `sorted` uses the runtime's own ordering.** There is no `Ordering`,
because resolving one needs implicits (D3). `sorted` orders numbers, strings,
`Char`s and `Boolean`s; for anything else it fails loudly instead of choosing
an arbitrary order.

Fixture: [`tests/conformance/tutorial/08-collections-sorted-needs-comparable.scala`](../../tests/conformance/tutorial/08-collections-sorted-needs-comparable.scala)

```scala
class Money(val cents: Int)

@main def run(): Unit =
  println(List(new Money(2), new Money(1)).sorted)
```

It stops with:

```text
08-collections-sorted-needs-comparable.scala:5: error: IllegalArgumentException: sorted needs comparable elements; use sortWith
```

In Scala this is a compile error until you supply an `Ordering[Money]`; here it
is a run-time error that names the remedy. `sortBy(_.cents)` and
`sortWith(_.cents < _.cents)` both work and are what the `Ordering` would have
said.

**D63 — there is no `collect`.** It takes a `PartialFunction`, which
protoScala does not have.

Fixture: [`tests/conformance/tutorial/08-collections-no-collect.scala`](../../tests/conformance/tutorial/08-collections-no-collect.scala)

```scala
@main def run(): Unit =
  println(List(1, 2, 3, 4).collect { case n if n % 2 == 0 => n * 10 })
```

It stops with:

```text
08-collections-no-collect.scala:3: error: NoSuchMethodError: value collect is not a member of List
```

Write the two steps `collect` fuses:

Fixture: [`tests/conformance/tutorial/08-collections-collect-rewritten.scala`](../../tests/conformance/tutorial/08-collections-collect-rewritten.scala)

```scala
@main def run(): Unit =
  println(List(1, 2, 3, 4).filter(_ % 2 == 0).map(_ * 10))
```

Prints:

```text
List(20, 40)
```

`filter(p).map(f)` traverses twice where `collect` traverses once, and says the
same thing. For a pattern rather than a predicate, `flatMap` with a `match`
that answers `List(x)` or `Nil` is the general form.

**D65 — `Seq` and `Iterable` are not provided.** `List`, `Vector`, `Range`,
`Map` and `Set` share no common ancestor, and there is no trait to name in a
type test or a signature.

Fixture: [`tests/conformance/tutorial/08-collections-seq-is-not-a-type.scala`](../../tests/conformance/tutorial/08-collections-seq-is-not-a-type.scala)

```scala
@main def run(): Unit =
  val v: Any = List(1, 2)
  val kind = v match
    case xs: Seq[_] => "a sequence"
    case _ => "something else"
  println(kind)
```

It is rejected with:

```text
08-collections-seq-is-not-a-type.scala:5:10: error: Not found: type Seq
```

A parameter annotated `xs: Seq[Int]` is harmless, because annotations are
erased (D4) — only a *test* fails. Match the concrete kinds
(`case xs: List[_] => … case xs: Vector[_] => …`), or write the method for
whatever arrives, since the surface is the same on all of them. `Seq` and
`Iterable` are not scheduled.

**D67 — `Either` and `Try` have no `withFilter`**, so a guard in a
comprehension over them fails.

Fixture: [`tests/conformance/tutorial/08-collections-either-has-no-withfilter.scala`](../../tests/conformance/tutorial/08-collections-either-has-no-withfilter.scala)

```scala
@main def run(): Unit =
  val r: Either[String, Int] = Right(2)
  val kept = for
    x <- r
    if x > 1
  yield x
  println(kept)
```

It stops with:

```text
08-collections-either-has-no-withfilter.scala:6: error: NoSuchMethodError: value withFilter is not a member of Right
```

Scala's `Either.withFilter` needs a `Left` to fall back to when the guard
fails, and it finds one through the static type — which protoScala does not
have (D4). Move the test into the comprehension's result, or use `filter` on
the `Option` you get from `toOption`. The failure is loud, not silent.

**D68 — `Range.map` answers a `List`.** Scala answers an `IndexedSeq`
(a `Vector` in practice), and so does a `for … yield` over a `Range` (§8.3).
The elements and their order are Scala's; only the wrapper differs, and
`IndexedSeq` is a `Seq` trait, which D65 rules out. `toVector` converts if the
kind matters.

**D71 — a key that overrides `equals` but not `hashCode` misses at once.**

Fixture: [`tests/conformance/tutorial/08-collections-equals-without-hashcode.scala`](../../tests/conformance/tutorial/08-collections-equals-without-hashcode.scala)

```scala
class Money(val cents: Int):
  override def equals(other: Any): Boolean =
    other.isInstanceOf[Money] && other.asInstanceOf[Money].cents == cents

@main def run(): Unit =
  val prices = Map(new Money(100) -> "a coffee", new Money(100) -> "a tea")
  println(prices.size.toString + " " + prices.getOrElse(new Money(100), "miss"))
```

Prints:

```text
2 miss
```

Overriding `equals` without `hashCode` is a bug in any language with hashed
maps, and this is what the bug looks like: two keys that are `==` hash
differently, so they occupy two slots and neither finds the other. Scala's
`Map1`…`Map4` compare small maps by `==` alone, so on the JVM this program
prints `1 a tea` and the defect only appears once the map reaches five
entries. protoScala is hashed from the first entry, so it reports the mistake
immediately. Override both, or let a `case class` synthesise both for you.

**Smaller differences,** for completeness:

- There is no `Ordering`, no `CanBuildFrom`/`IterableOnce` machinery and no
  builder protocol: a method's result kind is decided by the method.
- There is no `Array`. Varargs arrive as a `List` (D12) and `String.split`
  answers a `List` (D69, chapter 10).
- Not implemented on sequences: `scanLeft`/`scanRight`, `sliding`, `grouped`,
  `indexWhere`, `startsWith`, `toArray`. On `Map`: `--`, `groupBy`,
  `withDefaultValue`, `toVector`. On `Try`: `filter`, `fold`, `failed`. On
  `Either`: `toSeq`, `left`. Each is additive and none is scheduled yet.
- `foldLeft` accepts Scala's `xs.foldLeft(z)(op)` and also the single-list
  spelling `xs.foldLeft(z, op)`. The first is the one to write; the second is
  permissive, not divergent.
- `Map`, `Set`, `Vector` and `Range` are ordinary global names supplied by the
  runtime, not classes in a package, so a top-level `val Map = 1` replaces the
  companion for the rest of the file (D25). Shadowing them is legal and
  silent; do not.
- `Failure` carries the `Throwable` itself, exactly as Scala's does, so
  `recover` and `recoverWith` receive the exception and can pattern-match on it
  (chapter 11). Until Phase 4 it carried a `RuntimeError` case class; that
  deviation (D44) is retired.
