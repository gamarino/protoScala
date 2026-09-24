// The protoScala prelude: definitions every program sees, compiled when a
// session starts (DESIGN §6: the prelude is written in protoScala). Phase 2
// defines Option; Phase 3 extends it (Either, Try, more collection methods).
// `__raise` is an internal primitive that stands in for `throw` until
// exceptions exist.

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
  def get: Nothing = __raise("NoSuchElementException", "None.get")

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

// Phase 5 shipped Try/Success/Failure early (D44) because there were no
// exception values yet. Phase 3 only ADDS combinators; it must not re-point
// Failure's payload. Phase 4 replaces RuntimeError with a real Throwable in one
// edit here plus the `await` raise path, and closes D44 then (plan A0-13).
final case class RuntimeError(className: String, message: String):
  override def toString: String = className + ": " + message

sealed abstract class Try[+A]:
  def isSuccess: Boolean
  def isFailure: Boolean = !isSuccess
  def get: A
  def getOrElse[B >: A](default: B): B = if isSuccess then get else default
  def toOption: Option[A] = if isSuccess then Some(get) else None
  // Phase 3. There is no `filter`/`withFilter`: a failed filter needs an
  // exception value, which Phase 4 brings (D67).
  def map[B](f: A => B): Try[B]
  def flatMap[B](f: A => Try[B]): Try[B]
  def foreach[U](f: A => U): Unit
  def recover[B >: A](f: RuntimeError => B): Try[B]
  def recoverWith[B >: A](f: RuntimeError => Try[B]): Try[B]
  def orElse[B >: A](alternative: Try[B]): Try[B] = if isSuccess then this else alternative
  def toEither: Either[RuntimeError, A]

final case class Success[+A](value: A) extends Try[A]:
  def isSuccess: Boolean = true
  def get: A = value
  def map[B](f: A => B): Try[B] = __tryOf(() => f(value))
  def flatMap[B](f: A => Try[B]): Try[B] = f(value)
  def foreach[U](f: A => U): Unit = f(value)
  def recover[B >: A](f: RuntimeError => B): Try[B] = this
  def recoverWith[B >: A](f: RuntimeError => Try[B]): Try[B] = this
  def toEither: Either[RuntimeError, A] = Right(value)

final case class Failure[+A](error: RuntimeError) extends Try[A]:
  def isSuccess: Boolean = false
  def get: A = __raise(error.className, error.message)
  def map[B](f: A => B): Try[B] = Failure(error)
  def flatMap[B](f: A => Try[B]): Try[B] = Failure(error)
  def foreach[U](f: A => U): Unit = ()
  def recover[B >: A](f: RuntimeError => B): Try[B] = __tryOf(() => f(error))
  def recoverWith[B >: A](f: RuntimeError => Try[B]): Try[B] = f(error)
  def toEither: Either[RuntimeError, A] = Left(error)

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

// Constructors the runtime's native methods call. A native cannot name a
// global directly (a REPL redefinition gives `Some#1`, D25), so it resolves
// these through the global table once, after the prelude is compiled.
def __mkSome[A](v: A): Option[A] = Some(v)
def __mkNone: Option[Nothing] = None
def __mkSuccess[A](v: A): Try[A] = Success(v)
def __mkFailure[A](e: RuntimeError): Try[A] = Failure(e)
def __mkRuntimeError(c: String, m: String): RuntimeError = RuntimeError(c, m)
def __mkActorStats(w: Int, m: Int): ActorStats = ActorStats(w, m)
def __mkLeft[A, B](v: A): Either[A, B] = Left(v)
def __mkRight[A, B](v: B): Either[A, B] = Right(v)
