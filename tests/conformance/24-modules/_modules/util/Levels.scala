// A module that defines an `enum`. Every module file is desugared into an
// `object` (D91), so an `enum` that does not survive that lift makes an entire
// class of module unusable.
enum Level:
  case Debug, Info, Warn
def worst(): Level = Level.values.last
