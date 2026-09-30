# 18. Input and output: the terminal, other programs and the network

> **Implementation status.** Everything in this chapter runs today, in the
> interpreter and (except where a snippet uses named arguments, D121) in a
> transpiled module: standard input, the environment and the exit status,
> binary files and directories, other programs, TCP, TLS and UDP sockets, and an
> HTTP client and server. All of it is built on **protoIO**, the I/O library the
> protoCore runtimes share, so protoST, protoScala and protoClojure fail the same
> way on the same input. What is not implemented: random access to a file, a TLS
> *server*, HTTP keep-alive, and process pipelines (`#|`). The departures from
> Scala are D124–D132, listed in §18.9.

Chapter 16 read and wrote files. This one is everything else a script talks to:
the person at the terminal, the environment it was started in, the programs it
runs, and other machines. The names are Scala's wherever Scala has the thing —
`scala.io.StdIn`, `scala.sys`, `scala.sys.process`, `java.net.Socket` — and
requests-scala's for HTTP, the client most Scala programmers reach for. There
are no packages in protoScala, so none of them needs an `import`.

## 18.1 Reading standard input

`StdIn.readLine()` answers the next line without its terminator, and `null`
once the input is exhausted — the same contract as Scala's and as Java's
`BufferedReader`. A loop that reads until `null` is the classic filter:

Fixture: [`tests/conformance/tutorial/18-io-count-lines.scala`](../../tests/conformance/tutorial/18-io-count-lines.scala)

```scala
var count = 0
var longest = ""
var line = StdIn.readLine()
while line != null do
  count += 1
  if line.length > longest.length then longest = line
  line = StdIn.readLine()
println(s"$count lines, the longest is '$longest'")
```

Run with nothing on its input (the test suite gives it `/dev/null`), it prints:

```text
0 lines, the longest is ''
```

and fed from a pipe, `printf 'one\nthree\ntwo\n' | protoscala count.scala`
prints `3 lines, the longest is 'three'` (`tests/cli/io-stdin.sh` runs exactly
that).

`Source.stdin.getLines()` is the same input as an `Iterator`, so the whole
toolbox of chapter 16 applies. Input is **streamed**: each line is handed to
the program as soon as it arrives, and anything the program printed is flushed
before it waits, so a filter in a pipeline answers line by line.

Fixture: [`tests/conformance/tutorial/18-io-stdin-iterator.scala`](../../tests/conformance/tutorial/18-io-stdin-iterator.scala)

```scala
val numbers = Source.stdin.getLines().map(l => l.trim).filter(l => l.nonEmpty).map(l => l.toInt)
println(s"sum = ${numbers.sum}")
```

Prints, with no input:

```text
sum = 0
```

`StdIn.readInt()` and `readDouble()` read one line and parse it, raising a
`NumberFormatException` for a line that is not a number and an `EOFException`
at the end of input. A prompt written with `print` is visible before the
program waits:

```text
print("Your name? ")
val name = StdIn.readLine()
```

## 18.2 The program itself: arguments, environment, exit status

Arguments arrive in `@main`'s parameter list (chapter 1). The environment is
`sys.env`, an immutable `Map`; `sys.exit(n)` ends the program with status `n`
at once — nothing after it runs, not even a `finally`, exactly as on the JVM.
Errors go to standard error with `Console.err.println`.

Fixture: [`tests/conformance/tutorial/18-io-usage-and-exit.scala`](../../tests/conformance/tutorial/18-io-usage-and-exit.scala)

```scala
@main def greet(args: String*): Unit =
  if args.isEmpty then
    Console.err.println("usage: greet NAME...")
    sys.exit(2)
  val shell = sys.env.getOrElse("SHELL", "an unknown shell")
  for name <- args do println(s"Hello, $name, from $shell")
```

Run without arguments it fails, as a well-behaved command does:

```text
usage: greet NAME...
```

and the shell sees exit status 2. `sys.props("user.dir")` is the working
directory, and `sys.props` holds the handful of the JVM's standard properties
that mean something here (D127).

## 18.3 Binary files and directories

Text is a `String`; bytes are `Bytes`, protoScala's stand-in for
`Array[Byte]` (there is no `Array`, D69). A `Bytes` is immutable and compares by
content; its elements read as Scala's signed bytes, -128 to 127, and it can be
built from values in either -128..127 or 0..255.

Fixture: [`tests/conformance/tutorial/18-io-bytes.scala`](../../tests/conformance/tutorial/18-io-bytes.scala)

```scala
val png = Bytes(0x89, 0x50, 0x4E, 0x47)
FileIO.writeBytes("header.bin", png)
val back = FileIO.readBytes("header.bin")
println(s"${back.length} bytes, first ${back(0)}, same=${back == png}")
println("héllo".getBytes.length)
```

Prints:

```text
4 bytes, first -119, same=true
6
```

`0x89` reads back as `-119`, because a Scala `Byte` is signed; write
`b & 0xFF` when you want 0..255. `"héllo".getBytes` is six bytes, because `é`
takes two in UTF-8, and `bytes.utf8String` turns bytes back into text.

Directories follow `java.io.File` and `java.nio.file.Files`, as one-call
operations on `FileIO`:

Fixture: [`tests/conformance/tutorial/18-io-directories.scala`](../../tests/conformance/tutorial/18-io-directories.scala)

```scala
FileIO.deleteRecursively("project")
FileIO.mkdirs("project/src/main")
FileIO.write("project/src/main/App.scala", "@main def run() = println(1)\n")
FileIO.write("project/README.md", "# project\n")
FileIO.copy("project/README.md", "project/NOTES.md")
FileIO.move("project/NOTES.md", "project/TODO.md")
println(FileIO.list("project"))
println(s"${FileIO.isDirectory("project/src")} ${FileIO.size("project/README.md")}")
```

Prints:

```text
List(README.md, TODO.md, src)
true 10
```

`list` answers the names sorted. `move` and `copy` refuse to overwrite an
existing file with a `FileAlreadyExistsException`, as `Files.move` and
`Files.copy` do; pass `replaceExisting = true` when that is what you mean.
`deleteRecursively` removes a whole tree, and answers `false` when there was
nothing to remove.

## 18.4 Running other programs

`scala.sys.process`, in its most used shapes. `!` runs a command and answers
its exit code, with its output going wherever this program's output goes; `!!`
answers what it printed, and throws when it fails.

Fixture: [`tests/conformance/tutorial/18-io-processes.scala`](../../tests/conformance/tutorial/18-io-processes.scala)

```scala
val today = Process(List("date", "+%Y")).!!.trim
val status = Process(List("sh", "-c", "exit 3")).!
val sorted = (Process(List("sort")) #< "pear\napple\nfig\n").!!
println(s"${today.length} $status ${sorted.split("\n").toList}")
val failed =
  try Process(List("false")).!!
  catch case e: RuntimeException => e.getMessage
println(failed)
```

Prints:

```text
4 3 List(apple, fig, pear)
Nonzero exit value: 1
```

A command is a `List` (or `Vector`) of words — there is no `Seq` in protoScala
(D65) — or one `String`, which is split on spaces: `Process("ls -l")`, and the
shortcut `"ls -l".!` or `"ls -l".!!`. `#<` feeds the command a `String` or
`Bytes`, and it cannot kill your program by exiting without reading it: protoIO
turns the `SIGPIPE` such a program would otherwise deliver into an ordinary
error. `run()` starts a command and answers at once with a handle:
`exitValue()` waits for it, `destroy()` ends it.

## 18.5 Sockets

`Socket`, `ServerSocket` and `DatagramSocket` keep java.net's names, but not its
stream classes: a socket reads and writes text and bytes itself (D130). A server
socket bound to port `0` gets a free port from the system, which is the right
choice for anything that is not a well-known service, and for tests.

Fixture: [`tests/conformance/tutorial/18-io-tcp-echo.scala`](../../tests/conformance/tutorial/18-io-tcp-echo.scala)

```scala
val server = new ServerSocket(0, "127.0.0.1")
val worker = Thread.start { () =>
  val conn = server.accept()
  val line = conn.readLine()
  conn.write(line.reverse + "\n")
  conn.close()
}
val client = new Socket("127.0.0.1", server.getLocalPort)
client.write("stressed\n")
println(client.readLine())
println(client.readLine())
client.close()
worker.join()
server.close()
```

Prints:

```text
desserts
null
```

The second `readLine()` answers `null` because the server closed the
connection. Every wait can be bounded: after `setSoTimeout(ms)` a read that
waits longer raises `SocketTimeoutException`, and a connection nobody accepts
raises `ConnectException`. `startTls(hostName)` upgrades a connected socket to
TLS, with the server's certificate verified against the system's trust store.

## 18.6 An HTTP client

The client has requests-scala's shape, so the Scala you would write with that
library runs here: `requests.get(url)`, `requests.post(url, data = ...)`, and a
response with `statusCode`, `text()` and `headers`. The whole exchange — the
connection, TLS for `https`, redirects — is one call.

Fixture: [`tests/conformance/tutorial/18-io-http.scala`](../../tests/conformance/tutorial/18-io-http.scala)

```scala
val server = HttpServer(0) { req =>
  req.path match
    case "/hello" => Response(200, "Hello, " + req.query.getOrElse("name", "world"))
    case "/echo"  => Response.json(req.body)
    case _        => Response.notFound()
}
server.startInBackground()
val base = s"http://127.0.0.1:${server.port}"

val hello = requests.get(base + "/hello", Map(), Map("name" -> "Ada"))
println(s"${hello.statusCode} ${hello.text()}")
val echo = requests.post(base + "/echo", """{"answer": 42}""")
println(s"${echo.contentType.get} ${echo.text()}")
val missing =
  try requests.get(base + "/nowhere").statusCode
  catch case e: RequestFailedException => e.response.statusCode
println(missing)
server.stop()
```

Prints:

```text
200 Hello, Ada
application/json {"answer": 42}
404
```

A 4xx or 5xx status raises `RequestFailedException`, carrying the response, as
requests-scala does; pass `check = false` to have it answered instead. The
arguments are requests-scala's — `headers`, `params`, `data`, `readTimeout`,
`maxRedirects`, `check` — and are normally passed by name
(`requests.get(url, params = Map("q" -> "x"))`); the snippet passes them by
position only so that it also runs transpiled, which does not accept named
arguments yet (D121). The positional order is protoScala's own (D131).

The client follows redirects, but a redirect to **another** server drops the
headers you set — an `Authorization` meant for one host is never handed to
whichever host it names — and a redirect from `https` to `http` is refused.

## 18.7 An HTTP server

`HttpServer(port) { request => response }` is the server you saw above. It
listens as soon as it is built (so `port` is known; `0` picks a free one),
`start()` serves on the calling thread until `stop()`, and
`startInBackground()` serves on a thread of its own. Each connection is handled
on the actor pool, so a handler may itself make requests, read files or run
programs; the pool grows while a worker is blocked in I/O, so a server that
calls itself does not deadlock.

What reaches the handler has been checked first. A malformed request is answered
with **400**, a request line over 8 KiB with **414**, a header over 8 KiB (or more
than 100 of them) with **431**, and a body over the server's limit with **413** —
none of them runs your code. A handler that throws, or that answers a header
containing a line break (which could forge a second response), is answered with
a plain **500** and reported on standard error, and the server keeps serving.

Fixture: [`tests/conformance/tutorial/18-io-http-server-failures.scala`](../../tests/conformance/tutorial/18-io-http-server-failures.scala)

```scala
val server = HttpServer(0, "127.0.0.1", 16) { req =>
  if req.path == "/boom" then throw new IllegalStateException("boom")
  Response.text("fine")
}
server.startInBackground()
val base = s"http://127.0.0.1:${server.port}"
def status(path: String, body: String): Int =
  Requests.post(base + path, body, Map(), Map(), 30000, 5, false).statusCode
println(List(status("/ok", "small"), status("/ok", "x" * 100), status("/boom", "")))
server.stop()
```

Prints:

```text
List(200, 413, 500)
```

## 18.8 If you come from Python or JavaScript

| | Python | Node | protoScala |
|---|---|---|---|
| read a line | `input()` / `sys.stdin.readline()` | `readline` module | `StdIn.readLine()` (`null` at the end) |
| all lines | `for line in sys.stdin:` | `rl.on("line", ...)` | `for line <- Source.stdin.getLines() do` |
| arguments | `sys.argv[1:]` | `process.argv.slice(2)` | `@main def run(args: String*)` |
| environment | `os.environ["HOME"]` | `process.env.HOME` | `sys.env("HOME")` |
| exit status | `sys.exit(2)` | `process.exit(2)` | `sys.exit(2)` |
| run a program | `subprocess.run(["ls"])` | `execFileSync("ls")` | `Process(List("ls")).!` / `.!!` |
| bytes | `bytes` | `Buffer` | `Bytes` |
| TCP | `socket.create_connection` | `net.connect` | `new Socket(host, port)` |
| HTTP client | `requests.get(url)` | `fetch(url)` | `requests.get(url)` |
| HTTP server | `http.server` | `http.createServer` | `HttpServer(port) { req => ... }` |

Three habits to carry over. **Failures raise**: a refused connection is a
`ConnectException`, a timeout a `SocketTimeoutException`, a host that does not
exist an `UnknownHostException` — all of them `IOException`s, so one `catch`
covers them. **Nothing blocks the whole runtime**: there is no event loop and no
GIL, so a blocking read on one thread or actor leaves every other one running;
you do not need `async`/`await` to serve two clients at once. And **text is
UTF-8** everywhere (D125).

## 18.9 What differs from Scala 3

| | Scala 3 on the JVM | protoScala | id |
|---|---|---|---|
| `import scala.io.StdIn` and friends | required | not needed; the names are prelude globals | D8 |
| I/O exceptions | `java.io`, `java.net`, `java.nio.file` classes | the same simple names, in a slightly flatter tree; protoIO's messages | D124 |
| charsets | the platform default, or the one declared | UTF-8 always; lenient for streams, strict for a `Source` | D125 |
| binary data | `Array[Byte]` | `Bytes`, immutable, equal by content | D126 |
| `sys.props` | every JVM property, mutable | ten standard properties, read-only | D127 |
| `sys.exit` | runs shutdown hooks | ends the process at once | D128 |
| `Process(Seq(...))` | `Seq`, quoting, pipelines, loggers | `List`/`Vector` or a space-split `String`; `!`, `!!`, `#<`, `run()` | D129 |
| sockets | stream classes, `InetAddress`, `DatagramPacket` | methods on the socket, `String` addresses, `Datagram`, `tryAccept(ms)` | D130 |
| requests-scala | the full library | its common shape; own positional order; `HttpResponse` | D131 |
| an HTTP server | not in the standard library | `HttpServer`, one request per connection, served on actors | D132 |

---

Previous: [17. Compiled modules](17-compiled-modules.md) ·
Next: [Worked example](worked-example.md) ·
Up: [Tutorial index](../TUTORIAL.md)
