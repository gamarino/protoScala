# 13. Actors and futures

> Chapter 13 of [the protoScala tutorial](../TUTORIAL.md). Every runnable
> snippet below is, verbatim, a fixture under `tests/conformance/tutorial/`.

protoScala runs on protoCore, which has **no global interpreter lock**. Threads
are real OS threads, they run Scala code at the same time, and the collector is
concurrent. This chapter is about the concurrency model built on that: actors,
priority bands, and futures with a cooperative `await`.

## 13.1 Why actors here

An actor is a piece of state plus a function that handles one message at a
time. The runtime guarantees the **single-method invariant**: however many
threads send to an actor, only one message of that actor is ever being handled.
That is what makes shared state safe without a single lock in your program.

**Coming from Python.** This is not `asyncio`: there is no event loop and no
`async`/`await` colouring, and two actors really do run at the same time on two
cores. It is also not `multiprocessing`: a message is a pointer to an immutable
value, not a pickled copy, so sending a large list costs nothing.

**Coming from JavaScript.** This is not a `Worker` with `postMessage`: there is
no structured clone. The receiving actor sees the very same object graph the
sender had. Because protoScala values are immutable (D3), sharing it is safe.

**Coming from Scala on the JVM.** There is no Akka, no `ExecutionContext` and
no supervision. The surface is small and is described below; §13.10 lists every
departure with its `D` id.

## 13.2 An actor is a state and a handler

`Actor.spawn(initialState)(handler)` creates an actor. The handler takes the
current state and the incoming message, and returns either the pair
`(newState, reply)` or just the `newState` when there is nothing to reply
(D45) — a handler that only updates state does not have to invent a reply.

```scala
val sink = Actor.spawn(0) { (s, m) => s + m }   // no reply
sink ! 1
println((sink ? 7).await)                       // () -- a Future[Unit]
```

A `Tuple2` result is always read as `(newState, reply)`, so an actor whose state
is itself a pair returns it inside the pair form:
`{ (s, m) => ((s._1 + 1, s._2 + m), m) }`.

```scala
val counter = Actor.spawn(0) { (state, msg) =>
  (state + msg, state + msg)
}
var i = 0
while i < 10 do
  counter ! 1
  i += 1
var seen = 0
while seen < 10 do
  seen = counter.value
println(seen)
```

```
10
```

`counter.value` reads the state without sending a message, so it may lag behind
a send that is still queued (D51) — which is why the loop above waits for it.
The single-method invariant is what makes the final number exact: ten `!` leave
the counter at exactly ten, whatever the interleaving.

An actor prints as `Actor(<state>)`.

## 13.3 Telling and asking

`a ! msg` tells: it queues the message and returns `Unit` immediately.
`a ? msg` asks: it queues the message and returns a `Future` of the handler's
reply.

```scala
val acc = Actor.spawn(0) { (state, msg) => (state + msg, state + msg) }
acc ! 3
val total = (acc ? 4).await
println(total.toString + " " + acc.toString)
```

```
7 Actor(7)
```

Messages on the same band are handled in the order they were queued, so the ask
above is ordered behind the tell and sees `3` already added.

## 13.4 Priorities

Every actor has three mailboxes: `Priority.High`, `Priority.Medium` (the
default of `!` and `?`) and `Priority.Low`. `send(msg, priority)` and
`ask(msg, priority)` choose the band.

```scala
val inbox = Actor.spawn("") { (state, msg) => (msg.toString, msg.toString) }
inbox.send("routine", Priority.Low)
val answer = inbox.ask("urgent", Priority.High).await
println(answer)
```

```
urgent
```

A band guarantees **order within itself** and that a worker drains a
higher band before a lower one when it picks the next message. It does **not**
pre-empt: a message already being handled runs to completion. `Priority.High`,
`Medium` and `Low` are the integers `0`, `1` and `2` on an object, not an
`enum` (D52) — `enum` arrives in Phase 4.

## 13.5 Futures

`?` gives you a `Future`. A future is pending until it is completed, and then
carries either a value or an error.

| member | meaning |
|---|---|
| `f.await` | the value; raises the error of a failed future |
| `f.isCompleted` | `Boolean` |
| `f.value` | `Option[Try[T]]`: `None` while pending |
| `f.map(g)` | a new future completed with `g(value)`; a failure passes through |
| `f.flatMap(g)` | `g(value)` must return a `Future`; its completion completes the result |
| `f.recover(g)` | a failure becomes `g(error)`; a success passes through |
| `f.onComplete(g)` | `g(Try[T])`, the primitive the three above are built on |

`Future(expr)` runs a computation on the worker pool. The body is taken **by
name** (D47), exactly as in Scala, so the block form `Future { … }` works too and
nothing is evaluated on the calling thread.

```scala
val echo = Actor.spawn(0) { (s, m) => (s, m) }
val chained = (echo ? 4).map(x => x + 1).flatMap(x => Future(x * 5))
val recovered = Future(1 / 0).recover(e => -1)
println(chained.await.toString + " " + recovered.await.toString)
```

```
25 -1
```

Continuations run on the thread that completes the future, or immediately on
the caller when it is already complete — there is no `ExecutionContext` to
choose (D48).

## 13.6 `await` inside an actor is cooperative

This is the part that makes small worker pools usable. When a handler calls
`await` on a pending future, the runtime **snapshots the handler's call chain**,
releases the worker and parks the actor. The actor stays claimed, so the
messages behind it wait; when the future completes, the chain is rebuilt and
the handler continues exactly where it was.

```scala
val doubler = Actor.spawn(0) { (s, m) => (s, m * 2) }
val caller = Actor.spawn(0) { (s, m) =>
  val doubled = (doubler ? m).await
  (s + doubled, s + doubled)
}
println((caller ? 42).await)
```

```
84
```

Its fixture runs with `PROTOSCALA_ACTOR_WORKERS=1`. With one worker and a
*blocking* `await`, `caller` would hold the only worker while waiting for
`doubler`, which needs that same worker — a deadlock. It completes because the
worker is released at the suspension.

**The limits (D43).** A chain can be snapshotted only while every frame in it
is protoScala bytecode stopped at a call instruction. An `await` under a native
higher-order method (`List.map`, `foreach`, `withFilter`), inside a `Future`
continuation, or under an instruction that calls back into Scala without being
a call site (a user `equals` reached by `==`) raises
`UnsupportedOperationException` instead of suspending. The ask's future receives
that failure and the actor stays alive. Outside an actor — on the main thread
or inside `Thread.start` — `await` simply blocks, which is always safe.

## 13.7 When a handler fails

A handler that raises completes that message's future with
`Failure(RuntimeError(className, message))`, reports the error on stderr, and
leaves the actor alive with its previous state. There is no supervision in
0.3.0.

```scala
val fragile = Actor.spawn(0) { (s, m) => (s, s / m) }
val f = fragile ? 0
while !f.isCompleted do ()
val name = f.value match
  case Some(Failure(e)) => e.className
  case other            => "none"
println(name + " " + fragile.value.toString)
```

```
ArithmeticException 0
```

Until exceptions arrive in Phase 4 there are no exception *values*, so a failed
future carries a `RuntimeError` case class and `Try`/`Success`/`Failure` wrap it
(D44). Every value a handler returns is a valid result — a pair, or a bare new
state (D45) — so the only rejected result is no value at all, which fails that
message the same way, with `IllegalArgumentException`.

## 13.8 Threads and time

`Thread.start(() => body)` starts a real OS thread inside the collector's
quorum, and `t.join()` waits for it. `System.nanoTime()` is a monotonic clock
(only differences are meaningful) and `System.currentTimeMillis()` is wall
time. These are runtime facilities, not the JVM's (D49).

```scala
val counter = Actor.spawn(0) { (s, m) => (s + m, s + m) }
val started = System.nanoTime()
val threads = List(1, 2).map { _ =>
  Thread.start { () =>
    var i = 0
    while i < 1000 do
      counter ! 1
      i += 1
  }
}
threads.foreach(t => t.join())
val total = (counter ? 0).await
println(total.toString + " " + (System.nanoTime() >= started).toString)
```

```
2000 true
```

Prefer an actor when you want state; use `Thread.start` when you genuinely need
an independent thread that is not a worker — a producer feeding the pool, for
instance. `Future(…)` runs on the pool and never starts a thread of its
own.

## 13.9 Tuning and looking inside

The pool starts on the **first** `Actor.spawn`, so a script that uses no actor
pays nothing at start-up. `PROTOSCALA_ACTOR_WORKERS` sets the worker count; the
default is `max(2, cores − 2)`, capped at 16.

```scala
val a = Actor.spawn(0) { (s, m) => (s + 1, s + 1) }
val answer = (a ? 1).await
val stats = Actor.stats
println((answer == 1 && stats.workers >= 1 && stats.messagesProcessed >= 1).toString)
```

```
true
```

`Actor.stats` returns `ActorStats(workers, messagesProcessed)`, and
`Actor.isActor(x)` tests whether a value is an actor. The measured shape of the
seven benchmark modes — where more workers help and where they do not — is in
[benchmarks/RESULTS.md](../../benchmarks/RESULTS.md); it is measured, never
copied by hand into this chapter.

## 13.10 What differs from Scala 3

| Id | Deviation |
|---|---|
| D43 | `await` suspends only a chain of protoScala frames each stopped at a call instruction; inside a native higher-order method or a `Future` continuation it raises `UnsupportedOperationException` |
| D44 | There are no exception values yet: a failed `Future` carries `RuntimeError(className, message)`, wrapped by `Try`/`Success`/`Failure` |
| D45 | An actor handler returns `(newState, reply)` or a bare `newState`; with no reply, `?` completes with `()`. A `Tuple2` is always read as the pair form |
| D46 | An actor lives as long as the session; `Future.apply` creates one actor per call |
| D47 | `Future.apply` takes its body by name: `Future(expr)`, `Future { … }` |
| D53 | A by-name parameter is honoured only where the compiler can name the callee; elsewhere the argument is evaluated (chapter 5, §5.9) |
| D48 | Continuations run on the thread that completes the future (or on the caller when it is already complete); there is no `ExecutionContext`, and a continuation may not `await` |
| D49 | `Thread` and `System` are runtime facilities, not the JVM's |
| D50 | Awaiting a future that fails, inside an actor, abandons the rest of the handler; that message's future inherits the failure |
| D51 | `actor.value` reads the state without sending a message, so it may observe a state older than a queued send |
| D52 | `Priority.High` / `Medium` / `Low` are the integers `0` / `1` / `2` on an object, not an `enum` |

Also missing, by design in 0.3.0: supervision trees, `ExecutionContext`, actor
timeouts and `Await.result(f, duration)` — an `await` waits forever, and the
shutdown reports any actor still parked on a future that never completed.

---

Previous: [9. For-comprehensions](09-for-comprehensions.md) ·
Next: [14. The REPL and tooling](14-repl-and-tooling.md)
