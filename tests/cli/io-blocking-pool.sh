#!/usr/bin/env bash
#
# CLI check: blocking I/O on the actor pool cannot starve it. With ONE worker,
# a Future (which runs on that worker) makes an HTTP request to a server whose
# connections are served on the same pool. The request blocks the worker, so
# the server can answer only if the pool grows while a worker is blocked in
# I/O (docs/DECISIONS-LOG.md). Without that growth this program never ends.
#
# Usage: io-blocking-pool.sh <path-to-protoscala>
set -u
P="${1:?usage: io-blocking-pool.sh <protoscala>}"
work=$(mktemp -d -p "$PWD" io-pool.XXXXXX)
trap 'rm -rf "$work"' EXIT
cat >"$work/pool.scala" <<'SCALA'
val server = HttpServer(0) { req => Response(200, "served " + req.path) }
server.startInBackground()
val base = "http://127.0.0.1:" + server.port
val fs = (1 to 4).toList.map(i => Future(Requests.get(base + "/" + i).text()))
println(fs.map(_.await).mkString(","))
server.stop()
SCALA
out=$(PROTOSCALA_ACTOR_WORKERS=1 timeout 60s "$P" "$work/pool.scala" 2>&1); rc=$?
if [[ $rc -ne 0 || "$out" != "served /1,served /2,served /3,served /4" ]]; then
    echo "FAIL: exit $rc (124 is a timeout: the pool starved), output '$out'"
    exit 1
fi
echo OK
