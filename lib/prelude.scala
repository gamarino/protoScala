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

final case class Some[+A](value: A) extends Option[A]:
  def isEmpty: Boolean = false
  def get: A = value

case object None extends Option[Nothing]:
  def isEmpty: Boolean = true
  def get: Nothing = __raise("NoSuchElementException", "None.get")

// Phase 5: the result of a computation that may have failed. Phase 4 replaces
// RuntimeError with real exception values (D44).
final case class RuntimeError(className: String, message: String):
  override def toString: String = className + ": " + message

sealed abstract class Try[+A]:
  def isSuccess: Boolean
  def isFailure: Boolean = !isSuccess
  def get: A
  def getOrElse[B >: A](default: B): B = if isSuccess then get else default
  def toOption: Option[A] = if isSuccess then Some(get) else None

final case class Success[+A](value: A) extends Try[A]:
  def isSuccess: Boolean = true
  def get: A = value

final case class Failure[+A](error: RuntimeError) extends Try[A]:
  def isSuccess: Boolean = false
  def get: A = __raise(error.className, error.message)

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
