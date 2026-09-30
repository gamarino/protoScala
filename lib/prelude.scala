// The protoScala prelude: definitions every program sees, compiled when a
// session starts (DESIGN §6: the prelude is written in protoScala). Phase 2
// defines Option; Phase 3 extends it (Either, Try, more collection methods);
// Phase 4 adds the Throwable hierarchy and re-points Failure at it.

// Phase 4: the exception hierarchy (DESIGN §7). These are ordinary protoScala
// classes, so `case e: ArithmeticException` is the same per-class marker test as
// `case p: Point` (DESIGN §5.3), and the runtime materialises a native failure
// into one of them by unqualified name. The names are the JVM's without the
// `java.lang.` prefix; there is no `java` namespace (D8).
class Throwable(message: String):
  def getMessage: String = message
  def getCause: Throwable = null
  // getClass returns the class's simple name as a String: protoScala has no
  // Class[_] values, and `.getClass` on an exception is almost always fed to
  // string concatenation anyway.
  def getClass: String = __classNameOf(this)
  override def toString: String =
    if message == null then getClass else getClass + ": " + message

class Exception(message: String) extends Throwable(message)
class Error(message: String) extends Throwable(message)

class RuntimeException(message: String) extends Exception(message)
class ArithmeticException(message: String) extends RuntimeException(message)
class ClassCastException(message: String) extends RuntimeException(message)
class IllegalArgumentException(message: String) extends RuntimeException(message)
class IllegalStateException(message: String) extends RuntimeException(message)
class IndexOutOfBoundsException(message: String) extends RuntimeException(message)
class StringIndexOutOfBoundsException(message: String) extends IndexOutOfBoundsException(message)
class NoSuchElementException(message: String) extends RuntimeException(message)
class NullPointerException(message: String) extends RuntimeException(message)
class NumberFormatException(message: String) extends IllegalArgumentException(message)
class UnsupportedOperationException(message: String) extends RuntimeException(message)
// Track F: the exceptions file I/O raises. The hierarchy is the JVM's with the
// `java.io.` and `java.nio.charset.` prefixes dropped (D8): IOException under
// Exception (NOT under RuntimeException -- an I/O failure is checked on the JVM,
// and a Scala programmer expects `catch case e: IOException` to see a missing
// file AND a bad byte), FileNotFoundException and CharacterCodingException under
// it, MalformedInputException under that (D97).
class IOException(message: String) extends Exception(message)
class FileNotFoundException(message: String) extends IOException(message)
class CharacterCodingException(message: String) extends IOException(message)
class MalformedInputException(message: String) extends CharacterCodingException(message)
// The I/O track: the rest of java.io's, java.net's and java.nio.file's I/O
// exceptions that protoIO's error kinds map onto (D124), in the JVM's shape.
// FileAlreadyExistsException sits directly under IOException (the JVM has a
// FileSystemException between them, which nothing here raises).
class EOFException(message: String) extends IOException(message)
class FileAlreadyExistsException(message: String) extends IOException(message)
class InterruptedIOException(message: String) extends IOException(message)
class SocketTimeoutException(message: String) extends InterruptedIOException(message)
class SocketException(message: String) extends IOException(message)
class ConnectException(message: String) extends SocketException(message)
class UnknownHostException(message: String) extends IOException(message)
class MatchError(message: String) extends RuntimeException(message)
// scala.UninitializedFieldError extends RuntimeException despite its name, and
// matching that is free.
class UninitializedFieldError(message: String) extends RuntimeException(message)
// Phase 6. Scala has no ImportError, so nothing is diverged from; a failed
// module load needs a name that says what failed. Within protoScala an import is
// resolved at COMPILE time (D90), so a failed import is a compile error and not
// a catchable Throwable; this class is what a caller in ANOTHER runtime receives
// when a protoScala module it imported through UMD fails to load.
class ImportError(message: String) extends RuntimeException(message)
class InterruptedException(message: String) extends Exception(message)
class NoSuchMethodError(message: String) extends Error(message)
class StackOverflowError(message: String) extends Error(message)
class OutOfMemoryError(message: String) extends Error(message)
// Track D: what `assert` and `require` raise. Both extend `Error` in Scala
// (`java.lang.AssertionError`, `scala.NotImplementedError`), so `catch case
// e: Exception` does NOT catch them -- which is the whole point of an assertion.
class AssertionError(message: String) extends Error(message)
class NotImplementedError(message: String) extends Error(message)

// ---------------------------------------------------------------------------
// Track D: the Predef surface -- assert, assume, require, ???, App
// ---------------------------------------------------------------------------

// The value that means "no message was given". Scala's `assert` is two
// overloads and protoScala has none (D31), so one method takes a default --
// and the default must be a value no caller can write, because `null` IS a
// message Scala reports ("assertion failed: null").
object __NoMessage

// The exception text of a failed assertion: the bare prefix when no message was
// given, `prefix: message` when one was. The message is read exactly ONCE, as
// Scala reads its by-name argument once.
def __assertionText(prefix: String, message: Any): String =
  if message == __NoMessage then prefix
  else if message == null then prefix + ": null"
  else prefix + ": " + message.toString

// `assert`, `assume` and `require`, with Scala's exception types and Scala's
// exact message texts. The message is by-name, so it costs nothing when the
// condition holds. Scala's are macros that `-Xdisable-assertions` can elide;
// protoScala has no macros, so these are ordinary methods and there is no flag
// that removes them (D103).
def assert(cond: Boolean, message: => Any = __NoMessage): Unit =
  if !cond then throw new AssertionError(__assertionText("assertion failed", message))

def assume(cond: Boolean, message: => Any = __NoMessage): Unit =
  if !cond then throw new AssertionError(__assertionText("assumption failed", message))

// A failed `require` blames the CALLER, so it is an IllegalArgumentException
// and not an AssertionError -- and `-Xdisable-assertions` never elides it.
def require(cond: Boolean, message: => Any = __NoMessage): Unit =
  if !cond then throw new IllegalArgumentException(__assertionText("requirement failed", message))

// `???`: the placeholder for an unwritten implementation.
def ??? : Nothing = throw new NotImplementedError("an implementation is missing")

// `object Main extends App` runs the object's body as the program, the way
// Scala's App does. protoScala runs it after the file's top level, and only
// when the file defines no `@main` (D104). Deprecated in Scala 3 in favour of
// `@main`, and kept for the same reason Scala keeps it: it is what a decade of
// Scala teaching material writes.
trait App

sealed abstract class Option[+A]:
  def isEmpty: Boolean
  def get: A
  def isDefined: Boolean = !isEmpty
  def nonEmpty: Boolean = !isEmpty
  // The default is evaluated eagerly until by-name parameters exist (D33).
  def getOrElse[B >: A](default: B): B = if isEmpty then default else get
  def orElse[B >: A](alternative: Option[B]): Option[B] = if isEmpty then alternative else this
  def map[B](f: A => B): Option[B] = if isEmpty then None else Some(f(get))
  def flatMap[B](f: A => Option[B]): Option[B] = if isEmpty then None else f(get)
  def filter(p: A => Boolean): Option[A] = if isEmpty || p(get) then this else None
  def withFilter(p: A => Boolean): Option[A] = filter(p)
  def foreach[U](f: A => U): Unit = if !isEmpty then f(get)
  def contains[B >: A](elem: B): Boolean = !isEmpty && get == elem
  def exists(p: A => Boolean): Boolean = !isEmpty && p(get)
  def toList: List[A] = if isEmpty then Nil else get :: Nil
  // Phase 3.
  def fold[B](ifEmpty: B)(f: A => B): B = if isEmpty then ifEmpty else f(get)
  def toRight[X](left: X): Either[X, A] = if isEmpty then Left(left) else Right(get)
  def toLeft[X](right: X): Either[A, X] = if isEmpty then Right(right) else Left(get)
  def orNull: A = if isEmpty then null else get
  def forall(p: A => Boolean): Boolean = isEmpty || p(get)
  def count(p: A => Boolean): Int = if !isEmpty && p(get) then 1 else 0
  def zip[B](that: Option[B]): Option[(A, B)] =
    if isEmpty || that.isEmpty then None else Some((get, that.get))
  def iterator: List[A] = toList
  def toSeq: List[A] = toList

final case class Some[+A](value: A) extends Option[A]:
  def isEmpty: Boolean = false
  def get: A = value

case object None extends Option[Nothing]:
  def isEmpty: Boolean = true
  def get: Nothing = throw new NoSuchElementException("None.get")

// Phase 4: the receiver a custom string interpolator extends. `name"a${x}b"` is
// lowered to `StringContext("a", "b").name(x)`, exactly as Scala lowers it, and
// the interpolator itself is an extension method on this class — which is what
// closes Phase 3's restriction to `s`, `f` and `raw`.
final case class StringContext(parts: String*)

// Phase 4: a real enum, retiring D52. The ordinals are 0 / 1 / 2, which is
// exactly the band index the scheduler already uses, so nothing in
// ActorScheduler changes; `send`/`ask` accept a Priority case or a plain Int.
enum Priority:
  case High, Medium, Low

// Phase 3: the disjoint union. `map`/`flatMap`/`foreach` are right-biased, as
// they are in Scala 2.13 and Scala 3. There is no `withFilter`: Scala's needs a
// `Left` to fall back to, which needs the static type (D67).
sealed abstract class Either[+A, +B]:
  def isLeft: Boolean
  def isRight: Boolean = !isLeft
  def map[C](f: B => C): Either[A, C]
  def flatMap[C](f: B => Either[A, C]): Either[A, C]
  def foreach[U](f: B => U): Unit
  def getOrElse[C >: B](default: C): C
  def fold[C](fa: A => C, fb: B => C): C
  def swap: Either[B, A]
  def toOption: Option[B]
  def toList: List[B]
  def exists(p: B => Boolean): Boolean
  def forall(p: B => Boolean): Boolean
  def contains[C >: B](elem: C): Boolean

final case class Left[+A, +B](value: A) extends Either[A, B]:
  def isLeft: Boolean = true
  def map[C](f: B => C): Either[A, C] = Left(value)
  def flatMap[C](f: B => Either[A, C]): Either[A, C] = Left(value)
  def foreach[U](f: B => U): Unit = ()
  def getOrElse[C >: B](default: C): C = default
  def fold[C](fa: A => C, fb: B => C): C = fa(value)
  def swap: Either[B, A] = Right(value)
  def toOption: Option[B] = None
  def toList: List[B] = Nil
  def exists(p: B => Boolean): Boolean = false
  def forall(p: B => Boolean): Boolean = true
  def contains[C >: B](elem: C): Boolean = false

final case class Right[+A, +B](value: B) extends Either[A, B]:
  def isLeft: Boolean = false
  def map[C](f: B => C): Either[A, C] = Right(f(value))
  def flatMap[C](f: B => Either[A, C]): Either[A, C] = f(value)
  def foreach[U](f: B => U): Unit = f(value)
  def getOrElse[C >: B](default: C): C = value
  def fold[C](fa: A => C, fb: B => C): C = fb(value)
  def swap: Either[B, A] = Left(value)
  def toOption: Option[B] = Some(value)
  def toList: List[B] = value :: Nil
  def exists(p: B => Boolean): Boolean = p(value)
  def forall(p: B => Boolean): Boolean = p(value)
  def contains[C >: B](elem: C): Boolean = value == elem

// Phase 5 shipped Try/Success/Failure early with a RuntimeError payload (D44),
// because there were no exception values yet. Phase 4 re-points Failure at a
// real Throwable and retires D44; RuntimeError is gone.
sealed abstract class Try[+A]:
  def isSuccess: Boolean
  def isFailure: Boolean = !isSuccess
  def get: A
  def getOrElse[B >: A](default: B): B = if isSuccess then get else default
  def toOption: Option[A] = if isSuccess then Some(get) else None
  // Phase 3. There is no `filter`/`withFilter`: Scala's needs the static type
  // to build the failure's message (D67).
  def map[B](f: A => B): Try[B]
  def flatMap[B](f: A => Try[B]): Try[B]
  def foreach[U](f: A => U): Unit
  def recover[B >: A](f: Throwable => B): Try[B]
  def recoverWith[B >: A](f: Throwable => Try[B]): Try[B]
  def orElse[B >: A](alternative: Try[B]): Try[B] = if isSuccess then this else alternative
  def toEither: Either[Throwable, A]

final case class Success[+A](value: A) extends Try[A]:
  def isSuccess: Boolean = true
  def get: A = value
  def map[B](f: A => B): Try[B] = __tryOf(() => f(value))
  def flatMap[B](f: A => Try[B]): Try[B] = f(value)
  def foreach[U](f: A => U): Unit = f(value)
  def recover[B >: A](f: Throwable => B): Try[B] = this
  def recoverWith[B >: A](f: Throwable => Try[B]): Try[B] = this
  def toEither: Either[Throwable, A] = Right(value)

final case class Failure[+A](exception: Throwable) extends Try[A]:
  def isSuccess: Boolean = false
  def get: A = throw exception
  def map[B](f: A => B): Try[B] = Failure(exception)
  def flatMap[B](f: A => Try[B]): Try[B] = Failure(exception)
  def foreach[U](f: A => U): Unit = ()
  def recover[B >: A](f: Throwable => B): Try[B] = __tryOf(() => f(exception))
  def recoverWith[B >: A](f: Throwable => Try[B]): Try[B] = f(exception)
  def toEither: Either[Throwable, A] = Left(exception)

// Try { risky() }: the argument is by-name, so the block is re-evaluated inside
// the primitive's catch. The function form Try(() => expr) also works, because a
// zero-argument function handed to a by-name parameter is already a thunk (D47).
// No deviation is recorded: by-name parameters landed on main (bca0352), so
// plan A0-12 resolves to this form and its fallback D64 stays unused. D53's
// limit applies either way -- a by-name argument is lazy only where the
// compiler resolves the call site to the declaration, which a prelude object's
// method is.
object Try:
  def apply[A](body: => A): Try[A] = __tryOf(() => body)

// Phase 5: the value Actor.stats returns.
final case class ActorStats(workers: Int, messagesProcessed: Int)

// ---------------------------------------------------------------------------
// Track F and the I/O track: reading and writing files, standard input
// ---------------------------------------------------------------------------

// `scala.collection.Iterator`, the subset a reader of lines needs (D100). It is
// consumed as it is read, as Scala's is. protoScala has no anonymous classes
// (D80), so each derived iterator is a named class below.
abstract class Iterator[+A]:
  def hasNext: Boolean
  def next(): A
  def isEmpty: Boolean = !hasNext
  def nonEmpty: Boolean = hasNext
  def iterator: Iterator[A] = this
  def foreach[U](f: A => U): Unit =
    while hasNext do f(next())
  def map[B](f: A => B): Iterator[B] = new __MappedIterator(this, f)
  def filter(p: A => Boolean): Iterator[A] = new __FilteredIterator(this, p, true)
  def filterNot(p: A => Boolean): Iterator[A] = new __FilteredIterator(this, p, false)
  def withFilter(p: A => Boolean): Iterator[A] = filter(p)
  // `f` may answer any collection with `toList` (a List, a Vector, an Option,
  // an Iterator).
  def flatMap[B](f: A => Any): Iterator[B] = new __FlatMappedIterator(this, f)
  def take(n: Int): Iterator[A] = new __TakeIterator(this, n)
  def drop(n: Int): Iterator[A] =
    var k = 0
    while k < n && hasNext do
      next()
      k += 1
    this
  def takeWhile(p: A => Boolean): Iterator[A] = new __TakeWhileIterator(this, p)
  def dropWhile(p: A => Boolean): Iterator[A] = new __DropWhileIterator(this, p)
  def zipWithIndex: Iterator[(A, Int)] = new __ZipWithIndexIterator(this)
  def toList: List[A] =
    var acc: List[A] = Nil
    while hasNext do acc = next() :: acc
    acc.reverse
  def toSeq: List[A] = toList
  def toVector: Vector[A] = toList.toVector
  def toSet: Set[A] = toList.toSet
  def length: Int =
    var n = 0
    while hasNext do
      next()
      n += 1
    n
  def size: Int = length
  def count(p: A => Boolean): Int =
    var n = 0
    while hasNext do
      if p(next()) then n += 1
    n
  def exists(p: A => Boolean): Boolean =
    var found = false
    while !found && hasNext do found = p(next())
    found
  def forall(p: A => Boolean): Boolean = !exists(x => !p(x))
  def contains(elem: Any): Boolean = exists(x => x == elem)
  def find(p: A => Boolean): Option[A] =
    var r: Option[A] = None
    while r.isEmpty && hasNext do
      val x = next()
      if p(x) then r = Some(x)
    r
  def foldLeft[B](z: B)(op: (B, A) => B): B =
    var acc = z
    while hasNext do acc = op(acc, next())
    acc
  def sum: Any = toList.sum
  def max: A = toList.max
  def min: A = toList.min
  // Scala's three mkString overloads as one variadic method (D31): mkString,
  // mkString(sep) and mkString(start, sep, end).
  def mkString(parts: String*): String =
    if parts.length == 0 then toList.mkString
    else if parts.length == 1 then toList.mkString(parts(0))
    else if parts.length == 3 then toList.mkString(parts(0), parts(1), parts(2))
    else throw new IllegalArgumentException("mkString takes 0, 1 or 3 arguments, got " + parts.length)
  override def toString: String = "<iterator>"

final class __ListIterator[A](xs: List[A]) extends Iterator[A]:
  private var rest = xs
  def hasNext: Boolean = rest.nonEmpty
  def next(): A =
    if rest.isEmpty then throw new NoSuchElementException("next on empty iterator")
    val h = rest.head
    rest = rest.tail
    h

final class __MappedIterator[A, B](it: Iterator[A], f: A => B) extends Iterator[B]:
  def hasNext: Boolean = it.hasNext
  def next(): B = f(it.next())

final class __FilteredIterator[A](it: Iterator[A], p: A => Boolean, keep: Boolean) extends Iterator[A]:
  private var ready = false
  private var item: Any = null
  def hasNext: Boolean =
    while !ready && it.hasNext do
      val x = it.next()
      if p(x) == keep then
        item = x
        ready = true
    ready
  def next(): A =
    if !hasNext then throw new NoSuchElementException("next on empty iterator")
    ready = false
    item

final class __FlatMappedIterator[A, B](it: Iterator[A], f: A => Any) extends Iterator[B]:
  private var current: List[Any] = Nil
  def hasNext: Boolean =
    while current.isEmpty && it.hasNext do current = f(it.next()).toList
    current.nonEmpty
  def next(): B =
    if !hasNext then throw new NoSuchElementException("next on empty iterator")
    val h = current.head
    current = current.tail
    h

final class __TakeIterator[A](it: Iterator[A], n: Int) extends Iterator[A]:
  private var left = n
  def hasNext: Boolean = left > 0 && it.hasNext
  def next(): A =
    if left <= 0 then throw new NoSuchElementException("next on empty iterator")
    left -= 1
    it.next()

final class __TakeWhileIterator[A](it: Iterator[A], p: A => Boolean) extends Iterator[A]:
  private var ready = false
  private var done = false
  private var item: Any = null
  def hasNext: Boolean =
    if !ready && !done then
      if it.hasNext then
        val x = it.next()
        if p(x) then
          item = x
          ready = true
        else done = true
      else done = true
    ready
  def next(): A =
    if !hasNext then throw new NoSuchElementException("next on empty iterator")
    ready = false
    item

final class __DropWhileIterator[A](it: Iterator[A], p: A => Boolean) extends Iterator[A]:
  private var started = false
  private var ready = false
  private var item: Any = null
  def hasNext: Boolean =
    if !started then
      started = true
      var dropping = true
      while dropping && it.hasNext do
        val x = it.next()
        if !p(x) then
          item = x
          ready = true
          dropping = false
    ready || it.hasNext
  def next(): A =
    if !hasNext then throw new NoSuchElementException("next on empty iterator")
    if ready then
      ready = false
      item
    else it.next()

final class __ZipWithIndexIterator[A](it: Iterator[A]) extends Iterator[(A, Int)]:
  private var i = 0
  def hasNext: Boolean = it.hasNext
  def next(): (A, Int) =
    val r = (it.next(), i)
    i += 1
    r

// `scala.io.Source`'s reading surface, under the same names (there is no
// `scala.io` namespace to put it in, so it is a global, like every other prelude
// name). A file or standard-input source STREAMS: `getLines()` answers an
// Iterator that reads a line at a time, and the source is consumed as it is
// read, as Scala's is (D100, D101) -- `src.mkString` followed by
// `src.getLines()` gives an empty iterator. Reading a CLOSED source fails.
//
// `handle` names the native reading state of a file or of standard input
// (FilePrimitives.cpp); a `Source.fromString` source has none and reads `text`.
final class BufferedSource(text: String, origin: String, handle: Int = -1):
  private var isClosed = false
  // A string source: its lines, split on first use, and how many were read.
  private var lines: List[String] = null
  private var taken = 0
  private var drained = false
  def __check(): Unit =
    if isClosed then throw new IOException(origin + " (Stream Closed)")
  // The next line, or null at the end of input.
  def __readLine(): String =
    __check()
    if handle >= 0 then __srcLine(handle)
    else if drained then null
    else
      if lines == null then lines = __splitLines(text)
      if lines.isEmpty then null
      else
        val h = lines.head
        lines = lines.tail
        taken += 1
        h
  // Everything not read yet.
  def mkString: String =
    __check()
    if handle >= 0 then __srcRest(handle)
    else if drained then ""
    else
      drained = true
      if taken == 0 then text else __skipLines(text, taken)
  def getLines(): Iterator[String] =
    __check()
    new __SourceLines(this)
  def close(): Unit =
    if !isClosed && handle >= 0 then __srcClose(handle)
    isClosed = true
  def isOpen: Boolean = !isClosed
  override def toString: String = "BufferedSource"

final class __SourceLines(src: BufferedSource) extends Iterator[String]:
  private var fetched = false
  private var line: String = null
  def hasNext: Boolean =
    if !fetched then
      line = src.__readLine()
      fetched = true
    line != null
  def next(): String =
    if !hasNext then throw new NoSuchElementException("next on empty iterator")
    fetched = false
    line

object Source:
  // `enc` exists so that the common Scala spelling `Source.fromFile(p, "UTF-8")`
  // compiles and means what it says. protoScala decodes UTF-8 only, and any
  // other charset name is refused with an UnsupportedOperationException rather
  // than decoded as if it had been UTF-8 (D99). The file is opened here, so a
  // missing file fails here, as in Scala; it is read as the program asks.
  def fromFile(path: String, enc: String = "UTF-8"): BufferedSource =
    new BufferedSource(null, path, __srcOpen(path, enc))
  def fromString(s: String): BufferedSource = new BufferedSource(s, "<string>")
  // Standard input. Every Source.stdin (and StdIn.readLine) shares one reader,
  // so no source loses input another one read ahead.
  def stdin: BufferedSource = new BufferedSource(null, "<stdin>", __srcStdin())

// `scala.io.StdIn`. readLine answers null at the end of input; readInt and the
// others parse one line, and raise an EOFException at the end of input, as
// Scala's do. Anything printed so far is flushed first, so a prompt written with
// `print` is visible before the program waits. A malformed UTF-8 sequence reads
// as U+FFFD, as java.io's console reader decodes it (D125).
object StdIn:
  def readLine(prompt: String = null): String =
    if prompt != null then print(prompt)
    __stdinLine()
  private def line(): String =
    val s = __stdinLine()
    if s == null then throw new EOFException("Console has reached end of input")
    s
  def readInt(): Int = line().toInt
  def readLong(): Long = line().toLong
  def readDouble(): Double = line().toDouble
  def readBoolean(): Boolean = line().toBoolean

// Writing. Scala's writer is `java.io.PrintWriter` / `java.nio.file.Files`, and
// protoScala has no Java interop and will not have one, so this is protoScala's
// own explicit surface and NOT a simulation of either (D102). Each operation is
// one call, and each names its path in every failure:
//
//   FileIO.write(path, text)          replace the file's contents (creating it)
//   FileIO.append(path, text)         add to the end (creating it)
//   FileIO.exists(path)               is there anything at this path
//   FileIO.delete(path)               remove a file; false if there was none
//   FileIO.readBytes(path)            the whole file as Bytes
//   FileIO.writeBytes(path, bytes)    replace the contents with bytes
//   FileIO.list(dir)                  the entry names, sorted
//   FileIO.mkdirs(dir)                create with parents; false if it was there
//   FileIO.move(from, to[, replace])  rename; an existing target is refused
//   FileIO.copy(from, to[, replace])  copy a file or a tree; likewise
//   FileIO.isDirectory / isFile / size / lastModified (milliseconds)
//   FileIO.deleteRecursively(path)    remove a whole tree; false if nothing was there
//
// The text is written as UTF-8, which is what `Source.fromFile` reads back.
object FileIO:
  def write(path: String, text: String): Unit = __fileWriteText(path, text, false)
  def append(path: String, text: String): Unit = __fileWriteText(path, text, true)
  def exists(path: String): Boolean = __fileExists(path)
  // `true` when a file was removed, `false` when there was nothing at the path.
  // Every OTHER failure -- no permission, a directory, an I/O error -- raises an
  // IOException naming the path, because a `false` there would be a swallowed
  // error rather than an answer.
  def delete(path: String): Boolean = __fileDelete(path)
  def readBytes(path: String): Bytes = new Bytes(__fileReadBytes(path))
  // `data` is Bytes or any Seq of byte values (-128..255).
  def writeBytes(path: String, data: Any): Unit = __fileWriteBytes(path, Bytes.__bufferOf(data))
  def list(path: String): List[String] = __fileList(path)
  def mkdirs(path: String): Boolean = __fileMkdirs(path)
  def move(from: String, to: String, replaceExisting: Boolean = false): Unit =
    __fileMove(from, to, replaceExisting)
  def copy(from: String, to: String, replaceExisting: Boolean = false): Unit =
    __fileCopy(from, to, replaceExisting)
  def isDirectory(path: String): Boolean =
    val s = __fileStat(path)
    s != null && s(1)
  def isFile(path: String): Boolean =
    val s = __fileStat(path)
    s != null && s(0)
  def size(path: String): Long = statOf(path)(2)
  def lastModified(path: String): Long = statOf(path)(3)
  def deleteRecursively(path: String): Boolean = __fileRemoveTree(path)
  private def statOf(path: String): List[Any] =
    val s = __fileStat(path)
    if s == null then throw new FileNotFoundException(path + " (No such file or directory)")
    s

// ---------------------------------------------------------------------------
// The I/O track: binary data, the program, other programs, sockets and HTTP
// ---------------------------------------------------------------------------

// Binary data. Scala's is `Array[Byte]`; protoScala has no Array (D69), so a
// Bytes is an immutable sequence of byte values held in a protoCore byte
// buffer (D126). Its elements read as Scala's signed Byte values, -128..127;
// Bytes(...) and every writer accept -128..255, so 0xFF and -1 are one byte.
final class Bytes(__raw: Any):
  def __buffer: Any = __raw
  def length: Int = __bytesLength(__raw)
  def size: Int = __bytesLength(__raw)
  def isEmpty: Boolean = __bytesLength(__raw) == 0
  def nonEmpty: Boolean = __bytesLength(__raw) != 0
  def apply(i: Any): Int = i match
    case name: String =>
      throw new IllegalArgumentException("getBytes takes no charset: protoScala encodes UTF-8 only, write s.getBytes")
    case _ => __bytesAt(__raw, i)
  def slice(from: Int, until: Int): Bytes = new Bytes(__bytesSlice(__raw, from, until))
  def take(n: Int): Bytes = slice(0, n)
  def drop(n: Int): Bytes = slice(n, __bytesLength(__raw))
  def ++(other: Bytes): Bytes = new Bytes(__bytesConcat(__raw, other.__buffer))
  def toList: List[Int] = __bytesToList(__raw)
  def toSeq: List[Int] = toList
  def toVector: Vector[Int] = toList.toVector
  def map[B](f: Int => B): List[B] = toList.map(f)
  def foreach[U](f: Int => U): Unit = toList.foreach(f)
  // `new String(bytes, UTF_8)`: a malformed sequence decodes as U+FFFD.
  def utf8String: String = __bytesDecode(__raw)
  def sameElements(other: Bytes): Boolean = __bytesEquals(__raw, other.__buffer)
  override def equals(other: Any): Boolean = other match
    case b: Bytes => __bytesEquals(__raw, b.__buffer)
    case _ => false
  override def hashCode: Int = __bytesHash(__raw)
  override def toString: String = __bytesShow(__raw)

object Bytes:
  def apply(values: Int*): Bytes = new Bytes(__bytesFromSeq(values))
  def fromSeq(values: Any): Bytes = new Bytes(__bytesFromSeq(values))
  // The UTF-8 encoding of `s` ("s".getBytes).
  def fromString(s: String): Bytes = new Bytes(__bytesFromString(s))
  def empty: Bytes = new Bytes(__bytesFromSeq(Nil))
  // The buffer to hand a native: Bytes, or a Seq of byte values.
  def __bufferOf(data: Any): Any = data match
    case b: Bytes => b.__buffer
    case _ => __bytesFromSeq(data)

// `"text".getBytes`: the UTF-8 encoding. There is no charset argument, since
// UTF-8 is the only encoding (D99); `s.getBytes("UTF-8")` is refused by
// Bytes.apply with a message saying so (D126).
extension (s: String)
  def getBytes: Bytes = Bytes.fromString(s)

// Standard error, as `System.err` / `Console.err` in Scala.
object __StdErr:
  def print(x: Any): Unit = __ioStderr("" + x)
  def println(x: Any = ""): Unit = __ioStderr("" + x + "\n")

object Console:
  def err = __StdErr

// Name/value pairs from a native (a flat list) as a Map; the last value of a
// repeated name wins, as a dictionary filled in order would.
def __pairsToMap(xs: List[String]): Map[String, String] =
  var m: Map[String, String] = Map()
  var r = xs
  while r.nonEmpty do
    m = m.updated(r.head, r.tail.head)
    r = r.tail.tail
  m

// Name/value pairs as a Map from each name to all of its values, in order.
def __pairsToMultiMap(xs: List[String]): Map[String, List[String]] =
  var m: Map[String, List[String]] = Map()
  var r = xs
  while r.nonEmpty do
    m = m.updated(r.head, m.getOrElse(r.head, Nil) :+ r.tail.head)
    r = r.tail.tail
  m

def __flatPairs(m: Map[String, Any]): List[String] =
  m.toList.flatMap(kv => List(kv._1.toString, kv._2.toString))

// `scala.sys`: the running program.
object sys:
  // The environment, as an immutable Map (D127).
  def env: Map[String, String] = __pairsToMap(__ioEnv())
  // The JVM's standard system properties that mean something here, read-only
  // (D127).
  def props: Map[String, String] = __pairsToMap(__ioProps())
  // Ends the program at once with `status` (D128).
  def exit(status: Int = 0): Nothing = __ioExit(status)
  def error(message: String): Nothing = throw new RuntimeException(message)

// ---------------------------------------------------------------------------
// Other programs: scala.sys.process's shape (D129)
// ---------------------------------------------------------------------------

// A command to run. `!` runs it and answers its exit code, its output going to
// this program's own; `!!` answers its standard output and throws when it
// fails; `#<` gives it input; `run()` starts it and answers a Process.
final class ProcessBuilder(command: List[String], input: Any = null):
  def ! : Int = __procRunInherit(command, input)
  def !! : String =
    val r = __procCapture(command, input)
    if r(0) != 0 then throw new RuntimeException("Nonzero exit value: " + r(0))
    r(1)
  // The command's standard input: a String (sent as UTF-8) or Bytes.
  def #<(data: Any): ProcessBuilder = data match
    case b: Bytes => new ProcessBuilder(command, b.__buffer)
    case s: String => new ProcessBuilder(command, s)
    case other => throw new IllegalArgumentException("#< takes a String or Bytes, got " + other)
  def run(): Process =
    if input != null then
      throw new UnsupportedOperationException("run() with #< input is not supported; use ! or !!")
    new Process(__procSpawn(command))
  override def toString: String = "[" + command.mkString(", ") + "]"

// A running program, as `run()` answers it.
final class Process(pid: Int):
  private var code = -1
  private var finished = false
  // Waits for the program to end and answers its exit code (128 + the signal
  // when a signal ended it).
  def exitValue(): Int =
    if !finished then
      code = __procWait(pid)
      finished = true
    code
  // Asks the program to end (SIGTERM).
  def destroy(): Unit = if !finished then __procKill(pid, 15)
  override def toString: String = "Process(" + pid + ")"

object Process:
  // A command as a Seq of words, or as one String split on spaces.
  def apply(command: Any): ProcessBuilder = command match
    case s: String => new ProcessBuilder(s.split(" ").toList.filter(w => w.nonEmpty))
    case xs: List[Any] => new ProcessBuilder(xs.map(w => w.toString))
    case other => new ProcessBuilder(other.toList.map(w => w.toString))

// `"ls -l".!` and `"ls -l".!!`, scala.sys.process's implicit conversion.
extension (command: String)
  def ! : Int = Process(command).!
  def !! : String = Process(command).!!

// ---------------------------------------------------------------------------
// Sockets: java.net's names, with simplified streams (D130)
// ---------------------------------------------------------------------------

// A TCP connection. `new Socket(host, port)` connects; the reading and writing
// methods are on the socket itself rather than on an InputStream/OutputStream
// pair. Text is UTF-8; a malformed sequence read from a peer decodes as U+FFFD.
final class Socket(host: String, port: Int, connectTimeout: Int = -1, __accepted: Int = -1):
  private val fd: Int =
    if __accepted >= 0 then __accepted else __tcpConnect(host, port, connectTimeout)
  private var closed = false
  private def live(): Int =
    if closed then throw new SocketException("Socket is closed")
    fd
  // A line without its terminator, or null once the peer has closed.
  def readLine(): String = __fdReadLine(live(), 0)
  // Up to n whole characters, or null at the end.
  def read(n: Int): String = __fdRead(live(), n)
  // Up to n bytes, or null at the end.
  def readBytes(n: Int): Bytes =
    val b = __fdReadBytes(live(), n)
    if b == null then null else new Bytes(b)
  // Everything until the peer closes.
  def readAll(): String = __fdReadAll(live())
  def write(text: String): Unit = __fdWrite(live(), text)
  def writeBytes(data: Any): Unit = __fdWrite(live(), Bytes.__bufferOf(data))
  // Bounds every later read (and a TLS handshake); 0 waits without limit.
  def setSoTimeout(ms: Int): Unit = __fdSetTimeout(live(), if ms <= 0 then -1 else ms)
  // Upgrades the connection to TLS as a client; the certificate is verified
  // against the system's trust store and must name `serverName`.
  def startTls(serverName: String, verify: Boolean = true): Unit =
    __tlsConnect(live(), serverName, verify)
  def getLocalPort: Int = __sockName(live())(1)
  def getPort: Int = __peerName(live())(1)
  def getInetAddress: String = __peerName(live())(0)
  def isClosed: Boolean = closed
  def close(): Unit =
    if !closed then
      closed = true
      __fdClose(fd)
  override def toString: String =
    if closed then "Socket[closed]" else "Socket[addr=" + getInetAddress + ",port=" + getPort + ",localport=" + getLocalPort + "]"

object Socket:
  def apply(host: String, port: Int, connectTimeout: Int = -1): Socket =
    new Socket(host, port, connectTimeout)

// A TCP listener. The empty host listens on every interface, and port 0 on a
// free port (see getLocalPort).
final class ServerSocket(port: Int = 0, host: String = "", backlog: Int = 128):
  private val fd: Int = __tcpListen(host, port, backlog)
  private var closed = false
  private var timeout = -1
  def getLocalPort: Int = __sockName(fd)(1)
  def localPort: Int = getLocalPort
  // Bounds accept(); 0 waits without limit.
  def setSoTimeout(ms: Int): Unit = timeout = if ms <= 0 then -1 else ms
  // The next connection. With a timeout set, a SocketTimeoutException when
  // nobody connected in time, as java.net's accept does.
  def accept(): Socket =
    if closed then throw new SocketException("Socket is closed")
    val c = __tcpAccept(fd, timeout)
    if c >= 0 then new Socket(null, 0, -1, c)
    else if closed || timeout < 0 then throw new SocketException("Socket closed")
    else throw new SocketTimeoutException("Accept timed out")
  // The next connection within timeoutMs, or None (D130).
  def tryAccept(timeoutMs: Int): Option[Socket] =
    if closed then throw new SocketException("Socket is closed")
    val c = __tcpAccept(fd, if timeoutMs < 0 then -1 else timeoutMs)
    if c >= 0 then Some(new Socket(null, 0, -1, c)) else None
  def isClosed: Boolean = closed
  // Closing wakes a thread blocked in accept(), which then raises.
  def close(): Unit =
    if !closed then
      closed = true
      __fdClose(fd)
  override def toString: String = "ServerSocket[localport=" + getLocalPort + "]"

object ServerSocket:
  def apply(port: Int = 0, host: String = "", backlog: Int = 128): ServerSocket =
    new ServerSocket(port, host, backlog)

// A UDP datagram as received: its bytes, the text they hold, and the sender.
final case class Datagram(bytes: Bytes, host: String, port: Int):
  def data: String = bytes.utf8String

// A UDP socket bound to host:port (the empty host: every interface; port 0: a
// free one).
final class DatagramSocket(port: Int = 0, host: String = ""):
  private val fd: Int = __udpBind(host, port)
  private var closed = false
  private var timeout = -1
  private def live(): Int =
    if closed then throw new SocketException("Socket is closed")
    fd
  def getLocalPort: Int = __sockName(fd)(1)
  def localPort: Int = getLocalPort
  def setSoTimeout(ms: Int): Unit = timeout = if ms <= 0 then -1 else ms
  // Sends one datagram: a String (as UTF-8) or Bytes.
  def send(data: Any, toHost: String, toPort: Int): Unit = data match
    case s: String => __udpSend(live(), toHost, toPort, s)
    case other => __udpSend(live(), toHost, toPort, Bytes.__bufferOf(other))
  // The next datagram. With a timeout set, a SocketTimeoutException when
  // nothing arrived in time.
  def receive(): Datagram =
    val d = __udpReceive(live(), timeout)
    if d != null then Datagram(new Bytes(d(0)), d(1), d(2))
    else if closed || timeout < 0 then throw new SocketException("Socket closed")
    else throw new SocketTimeoutException("Receive timed out")
  // The next datagram within timeoutMs, or None.
  def tryReceive(timeoutMs: Int): Option[Datagram] =
    val d = __udpReceive(live(), if timeoutMs < 0 then -1 else timeoutMs)
    if d == null then None else Some(Datagram(new Bytes(d(0)), d(1), d(2)))
  def isClosed: Boolean = closed
  def close(): Unit =
    if !closed then
      closed = true
      __fdClose(fd)

// ---------------------------------------------------------------------------
// HTTP client: requests-scala's shape (D131)
// ---------------------------------------------------------------------------

// A response. `headers` maps each lower-case name to all of its values, in
// order, as requests-scala's does.
final class HttpResponse(val url: String, val statusCode: Int, val statusMessage: String,
                         val headers: Map[String, List[String]], __body: Any):
  def bytes: Bytes = new Bytes(__body)
  def contents: Bytes = new Bytes(__body)
  // The body as text (UTF-8; a malformed sequence decodes as U+FFFD).
  def text(): String = __bytesDecode(__body)
  def is2xx: Boolean = statusCode >= 200 && statusCode < 300
  def is3xx: Boolean = statusCode >= 300 && statusCode < 400
  def is4xx: Boolean = statusCode >= 400 && statusCode < 500
  def is5xx: Boolean = statusCode >= 500 && statusCode < 600
  def contentType: Option[String] = headers.get("content-type").map(vs => vs.last)
  def location: Option[String] = headers.get("location").map(vs => vs.last)
  override def toString: String = "Response(" + url + ", " + statusCode + ")"

class RequestsException(message: String) extends Exception(message)
// What a checked request raises for a 4xx or 5xx status; it carries the
// response.
class RequestFailedException(val response: HttpResponse)
    extends RequestsException("Request to " + response.url + " failed with status code " +
                              response.statusCode + "\n" + response.text())

// `requests.get(url)` and friends. Every method takes requests-scala's named
// arguments: headers, params (an encoded query string), data (a String, Bytes,
// or a Map sent as a form), readTimeout (milliseconds, for the connection and
// every wait), maxRedirects and check (raise RequestFailedException for a 4xx
// or 5xx status, true by default).
object Requests:
  def send(method: String, url: String, headers: Map[String, String] = Map(),
           params: Map[String, String] = Map(), data: Any = null, readTimeout: Int = 30000,
           maxRedirects: Int = 5, check: Boolean = true): HttpResponse =
    val full =
      if params.isEmpty then url
      else url + (if url.contains("?") then "&" else "?") + encodeForm(params)
    var hs = __flatPairs(headers)
    val named = headers.toList.map(kv => kv._1.toString.toLowerCase)
    val body: Any = data match
      case null => null
      case s: String => s
      case b: Bytes =>
        if !named.contains("content-type") then hs = hs ++ List("content-type", "application/octet-stream")
        b.__buffer
      case m: Map[Any, Any] =>
        if !named.contains("content-type") then
          hs = hs ++ List("content-type", "application/x-www-form-urlencoded")
        encodeForm(m)
      case other => other.toString
    val r = __httpRequest(method, full, hs, body, readTimeout, maxRedirects)
    val response = new HttpResponse(full, r(0), r(1), __pairsToMultiMap(r(2)), r(3))
    if check && response.statusCode >= 400 then throw new RequestFailedException(response)
    response
  def get(url: String, headers: Map[String, String] = Map(), params: Map[String, String] = Map(),
          readTimeout: Int = 30000, maxRedirects: Int = 5, check: Boolean = true): HttpResponse =
    send("GET", url, headers, params, null, readTimeout, maxRedirects, check)
  def head(url: String, headers: Map[String, String] = Map(), params: Map[String, String] = Map(),
           readTimeout: Int = 30000, maxRedirects: Int = 5, check: Boolean = true): HttpResponse =
    send("HEAD", url, headers, params, null, readTimeout, maxRedirects, check)
  def delete(url: String, headers: Map[String, String] = Map(), params: Map[String, String] = Map(),
             readTimeout: Int = 30000, maxRedirects: Int = 5, check: Boolean = true): HttpResponse =
    send("DELETE", url, headers, params, null, readTimeout, maxRedirects, check)
  def post(url: String, data: Any = null, headers: Map[String, String] = Map(),
           params: Map[String, String] = Map(), readTimeout: Int = 30000, maxRedirects: Int = 5,
           check: Boolean = true): HttpResponse =
    send("POST", url, headers, params, data, readTimeout, maxRedirects, check)
  def put(url: String, data: Any = null, headers: Map[String, String] = Map(),
          params: Map[String, String] = Map(), readTimeout: Int = 30000, maxRedirects: Int = 5,
          check: Boolean = true): HttpResponse =
    send("PUT", url, headers, params, data, readTimeout, maxRedirects, check)
  def patch(url: String, data: Any = null, headers: Map[String, String] = Map(),
            params: Map[String, String] = Map(), readTimeout: Int = 30000, maxRedirects: Int = 5,
            check: Boolean = true): HttpResponse =
    send("PATCH", url, headers, params, data, readTimeout, maxRedirects, check)
  // application/x-www-form-urlencoded, as java.net.URLEncoder writes it.
  def encodeForm(values: Map[Any, Any]): String =
    values.toList.map(kv => __urlEncode(kv._1.toString) + "=" + __urlEncode(kv._2.toString)).mkString("&")
  def decodeForm(text: String): Map[String, String] = __pairsToMap(__httpParseQuery(text))

// ---------------------------------------------------------------------------
// HTTP server (D132)
// ---------------------------------------------------------------------------

// What a handler receives. `path` is percent-decoded and `target` is the
// request target as sent; `query` and `headers` are Maps (header names in
// lower case; a repeated name keeps its last value); `body` is the body as
// text and `bodyBytes` as Bytes; `form` holds a form-encoded body's fields.
final class Request(val method: String, val target: String, val path: String,
                    val query: Map[String, String], val headers: Map[String, String],
                    val bodyBytes: Bytes):
  val body: String = bodyBytes.utf8String
  // The fields of a form-encoded body (application/x-www-form-urlencoded);
  // empty for any other body.
  val form: Map[String, String] =
    if headers.getOrElse("content-type", "").startsWith("application/x-www-form-urlencoded") then
      __pairsToMap(__httpParseQuery(body))
    else Map()
  override def toString: String = "Request(" + method + " " + target + ")"

// What a handler answers: a status, a body (a String or Bytes) and headers. A
// response without a content-type gets text/plain (UTF-8) for a String and
// application/octet-stream for Bytes.
final case class Response(status: Int, body: Any = "", headers: Map[String, String] = Map())

object Response:
  def ok(body: Any): Response = Response(200, body)
  def text(body: String, status: Int = 200): Response =
    Response(status, body, Map("Content-Type" -> "text/plain; charset=utf-8"))
  def json(body: String, status: Int = 200): Response =
    Response(status, body, Map("Content-Type" -> "application/json"))
  def html(body: String, status: Int = 200): Response =
    Response(status, body, Map("Content-Type" -> "text/html; charset=utf-8"))
  def redirect(location: String, status: Int = 302): Response =
    Response(status, "", Map("Location" -> location))
  def notFound(body: String = "Not Found"): Response = Response(404, body)

// An HTTP/1.1 server. It listens as soon as it is built (so `port` is known,
// and port 0 picks a free one); start() runs the accept loop on the calling
// thread until stop(), startInBackground() runs it on a Thread. Each connection
// is served on one of a fixed set of actors, round robin; a handler that blocks
// in I/O does not starve the pool, which grows while workers block. Requests
// the library refuses (400, 414, 431, 413) never reach the handler; a handler
// that throws, or answers an invalid response, gets a plain 500 and a report on
// standard error. One request per connection ("connection: close").
final class HttpServer(port0: Int, host: String, maxBodyBytes: Int, handler: Request => Any):
  private val fd: Int = __tcpListen(host, port0, 128)
  val port: Int = __sockName(fd)(1)
  private var started = false
  private var stopped = false
  private var background: Thread = null
  private var servers: Vector[Any] = Vector()
  private var next = 0

  def serve(c: Int): Unit =
    try
      val r = __httpReadRequest(c, maxBodyBytes)
      if r != null then
        val req = new Request(r(0), r(1), r(2), __pairsToMap(r(3)), __pairsToMap(r(4)), new Bytes(r(5)))
        val resp =
          try HttpServer.asResponse(handler(req))
          catch case e: Throwable =>
            __ioStderr("protoscala: HttpServer handler failed on " + req + ": " + e + "\n")
            Response(500, "Internal Server Error")
        try __httpWriteResponse(c, resp.status, __flatPairs(resp.headers), HttpServer.bodyOf(resp.body))
        catch case e: IllegalArgumentException =>
          __ioStderr("protoscala: HttpServer refused the handler's response to " + req + ": " +
                     e.getMessage + "\n")
          __httpWriteResponse(c, 500, Nil, "Internal Server Error")
    catch
      case e: IOException => ()  // the client went away
      case e: Throwable => __ioStderr("protoscala: HttpServer connection failed: " + e + "\n")
    finally __fdClose(c)

  private def begin(): Unit =
    if stopped then throw new IllegalStateException("HttpServer: already stopped")
    if started then throw new IllegalStateException("HttpServer: already started")
    started = true
    servers = (1 to HttpServer.connectionActors).toVector.map(_ =>
      Actor.spawn(0) { (st, c) =>
        serve(c)
        (st, ())
      })

  private def acceptLoop(): Unit =
    var go = true
    while go do
      val c = __tcpAccept(fd, -1)
      if c < 0 then go = false
      else
        servers(next % servers.length) ! c
        next += 1

  // Serves on the calling thread until stop() is called (from a handler or
  // another thread).
  def start(): Unit =
    begin()
    acceptLoop()

  // Serves on a Thread and answers at once.
  def startInBackground(): HttpServer =
    begin()
    background = Thread.start(() => acceptLoop())
    this

  // Stops accepting: the listening socket is closed, and a background accept
  // loop is joined. Requests already accepted are still answered.
  def stop(): Unit =
    if !stopped then
      stopped = true
      __fdClose(fd)
      if background != null then background.join()

  override def toString: String = "HttpServer(" + host + ":" + port + ")"

object HttpServer:
  // The number of actors connections are served on, round robin.
  def connectionActors: Int = 16
  def apply(port: Int, host: String = "127.0.0.1", maxBodyBytes: Int = 67108864)(
      handler: Request => Any): HttpServer =
    new HttpServer(port, host, maxBodyBytes, handler)
  def asResponse(v: Any): Response = v match
    case r: Response => r
    case s: String => Response(200, s)
    case b: Bytes => Response(200, b)
    case other => throw new IllegalStateException("an HttpServer handler must answer a Response, got " + other)
  def bodyOf(body: Any): Any = body match
    case b: Bytes => b.__buffer
    case s: String => s
    case null => ""
    case other => other.toString

// Constructors the runtime's native methods call. A native cannot name a
// global directly (a REPL redefinition gives `Some#1`, D25), so it resolves
// these through the global table once, after the prelude is compiled.
def __mkSome[A](v: A): Option[A] = Some(v)
def __mkNone: Option[Nothing] = None
def __mkSuccess[A](v: A): Try[A] = Success(v)
def __mkFailure[A](e: Throwable): Try[A] = Failure(e)
def __mkActorStats(w: Int, m: Int): ActorStats = ActorStats(w, m)
def __mkLeft[A, B](v: A): Either[A, B] = Left(v)
def __mkRight[A, B](v: B): Either[A, B] = Right(v)
