// The severity scale.
//
// A module file IS an object (D91), so everything below is reached from
// elsewhere as `Levels.Level` and `Levels.fromLabel`.
//
// The severity ORDER is the declaration order and nothing else: `ordinal` is
// what the report sorts by, so the scale is written down once, here, and no
// caller ever spells out a list of level names in the order it wants them
// printed. Adding a case adds a column to the report and changes nothing else.
enum Level:
  case Debug, Info, Warn, Error

  // The spelling that appears in the log. Deriving it from the case name means
  // a new case is readable from the log the moment it is declared.
  def label: String = toString.toUpperCase

  // The comparison a filter wants ("at least a warning"), given a name so that
  // no caller has to know that ordinals happen to increase with severity.
  def atLeast(other: Level): Boolean = this.ordinal >= other.ordinal

// Reading a label is a partial operation, and this is where it is allowed to
// fail: the caller wraps it, and an unknown label becomes a malformed line
// rather than a silently dropped one.
def fromLabel(label: String): Level =
  Level.values.find(l => l.label == label) match
    case Some(l) => l
    case None    => throw new IllegalArgumentException("unknown level '" + label + "'")
