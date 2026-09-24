// The input, carried as text.
//
// protoScala 0.6.0 has no file I/O: a program cannot open `sample.log` itself.
// So the log ships twice -- once as the real file, for a reader to look at, and
// once here, as the text this program actually parses. The two are kept honest
// mechanically: `tests/cli/examples.sh` diffs the block between the `"""`
// markers below against `sample.log` and fails if they ever drift apart.
val text: String = """
INFO auth user ada signed in
INFO auth user grace signed in
DEBUG cache warming 128 entries
WARN cache eviction rate 0.83 above threshold
ERROR db connection refused by replica 2
INFO http GET /orders 200
WARN http slow response 1.4s on /orders
ERROR db replica 2 still unreachable
INFO http GET /orders 200
ERROR queue dead letter for job 7741
INFO auth user ada signed out
*** truncated by logrotate ***
DEBUG cache 12 entries expired
ERROR
"""

// One entry per non-blank line. `split` here is a plain substring split, not a
// regular expression, so the blank first line the opening `"""` leaves behind
// is dropped by the filter rather than by a clever pattern.
def lines: List[String] = text.split("\n").toList.filter(l => l.trim.nonEmpty)
