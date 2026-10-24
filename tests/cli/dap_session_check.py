#!/usr/bin/env python3
"""CLI check: a debugging session over `protoscala --dap`, driven the way VS Code
drives it (docs/debugging.md).

Usage: dap_session_check.py <protoscala> [scenario ...]

Scenarios (all by default):
  session   breakpoints, the call stack, Locals and Globals, evaluate in a
            frame, step over / in / out, a breakpoint removed mid-session
  entry     stopOnEntry stops at the first line of the script
  output    the script's stdout and stderr arrive as `output` events: printed
            around a breakpoint, more than a pipe buffer of non-ASCII text,
            and stdin reads end of file instead of eating the protocol
  resolve   a breakpoint on a line with no code moves to the next one that
            has code and is reported verified once the file is compiled
  module    a breakpoint in an imported module's file
  pause     `pause` stops a running loop; `disconnect` ends a script that
            would never end by itself
  raised    the "raised" exception filter stops at a throw (and at an error
            the runtime raises) with the frame's locals; a handler that
            re-raises does not stop again; the program then ends normally
  internal  with "raised" on, iteration, Try, futures and an actor's `await`
            (a cooperative suspension) never stop; only the one real throw
  nofilter  no exception filter: no exception stop
  uncaught  the "userUnhandled" filter skips caught exceptions (and one Try
            captures) and stops where a throw no catch selects, stack intact
  threads   two threads stop at one breakpoint (two thread ids) while a third
            keeps printing; a step in one leaves the other stopped
  pausethread  `pause` of one busy thread; another keeps running
  gc        one thread stays stopped while another allocates enough for
            several collections; the stopped frame's locals are intact
  uncaughtthreads  "userUnhandled" stops a failing thread body and actor
            handler on their own threads, not a Future's body
  exccondition  a condition on an exception filter (`e` bound): true, false,
            one that throws, a syntax error; a breakpoint in a function the
            condition calls never stops
  linebp    line breakpoint `condition` and every `hitCondition` form, an
            invalid one, a condition that fails
  logpoint  `logMessage` with `{expr}`, `{{literal}}`, an expression error
  condthreads  a condition hit by two threads is evaluated in each
  embedded  a step into the prelude shows its embedded source by
            sourceReference; `source` serves it; a breakpoint on it stops;
            justMyCode keeps steps out of it; loadedSources
  pathmap   pathMappings with a fake client root: breakpoints and frames in
            client paths, a module outside the mapping by sourceReference
            (content, breakpoint), `source` by client path, a bad mapping

Paths are passed as VS Code passes them on Windows: absolute, with a
lower-case drive letter. Every read has a watchdog, so a broken adapter fails
instead of hanging.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time


class Fail(Exception):
    pass


ADAPTERS = []  # killed at the end, whatever a scenario left behind


def client_path(p):
    p = os.path.abspath(p)
    if os.name == "nt" and len(p) > 1 and p[1] == ":":
        p = p[0].lower() + p[1:]
    return p


class Dap:
    def __init__(self, exe, cwd, env=None):
        full_env = None
        if env:
            full_env = dict(os.environ)
            full_env.update(env)
        self.p = subprocess.Popen([exe, "--dap"], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=cwd,
                                  env=full_env)
        ADAPTERS.append(self.p)
        self.seq = 0
        self.messages = []
        self.cv = threading.Condition()
        self.closed = False
        self.unframed = None
        self.stderr = b""
        threading.Thread(target=self._reader, daemon=True).start()
        threading.Thread(target=self._stderr, daemon=True).start()
        self.output = {"stdout": "", "stderr": ""}

    def _stderr(self):
        self.stderr = self.p.stderr.read()

    def _reader(self):
        out = self.p.stdout
        try:
            while True:
                header = b""
                while not header.endswith(b"\r\n\r\n"):
                    c = out.read(1)
                    if not c:
                        return
                    header += c
                if not header.startswith(b"Content-Length: "):
                    self.unframed = header[:60]
                    return
                n = int(header[len(b"Content-Length: "):].split(b"\r\n")[0])
                body = out.read(n)
                msg = json.loads(body.decode("utf-8"))
                with self.cv:
                    if msg.get("event") == "output":
                        cat = msg["body"].get("category", "console")
                        self.output[cat] = self.output.get(cat, "") + msg["body"]["output"]
                    self.messages.append(msg)
                    self.cv.notify_all()
        finally:
            with self.cv:
                self.closed = True
                self.cv.notify_all()

    def send(self, command, arguments=None):
        self.seq += 1
        b = json.dumps({"seq": self.seq, "type": "request", "command": command,
                        "arguments": arguments or {}}).encode("utf-8")
        self.p.stdin.write(b"Content-Length: %d\r\n\r\n" % len(b) + b)
        self.p.stdin.flush()
        return self.seq

    def wait_for(self, pred, what, timeout=60):
        deadline = time.time() + timeout
        with self.cv:
            while True:
                for i, m in enumerate(self.messages):
                    if pred(m):
                        del self.messages[i]
                        return m
                if self.unframed is not None:
                    raise Fail("unframed bytes on the protocol channel: %r" % self.unframed)
                if self.closed:
                    raise Fail("adapter closed the channel while waiting for %s; stderr: %r"
                               % (what, self.stderr[-400:]))
                left = deadline - time.time()
                if left <= 0:
                    raise Fail("timed out waiting for %s" % what)
                self.cv.wait(left)

    def request(self, command, arguments=None, ok=True):
        s = self.send(command, arguments)
        r = self.wait_for(lambda m: m.get("type") == "response" and m.get("request_seq") == s,
                          "the response to " + command)
        if ok and not r.get("success"):
            raise Fail("%s failed: %s" % (command, r.get("message")))
        return r

    def event(self, name, timeout=60):
        return self.wait_for(lambda m: m.get("type") == "event" and m.get("event") == name,
                             "event " + name, timeout)

    def start(self, program, breakpoints=None, exceptions=None, **launch):
        caps = self.request("initialize", {"adapterID": "protoscala", "pathFormat": "path",
                                           "linesStartAt1": True})["body"]
        self.capabilities = caps
        self.event("initialized")
        args = {"program": client_path(program)}
        args.update(launch)
        self.request("launch", args)
        for path, lines in (breakpoints or {}).items():
            self.set_breakpoints(path, lines)
        if exceptions is not None:
            r = self.request("setExceptionBreakpoints", {"filters": exceptions})
            expect(all(b.get("verified") for b in r["body"]["breakpoints"]),
                   "exception filters %r" % r["body"])
        self.request("configurationDone")

    def set_breakpoints(self, path, lines):
        return self.request("setBreakpoints", {
            "source": {"path": client_path(path)},
            "breakpoints": [{"line": l} for l in lines]})["body"]["breakpoints"]

    def stopped(self, reason=None, line=None, timeout=60):
        ev = self.event("stopped", timeout)
        self.last_stop = ev["body"]
        if reason and ev["body"]["reason"] != reason:
            raise Fail("stopped for %r, expected %r (%r)" % (ev["body"]["reason"], reason, ev["body"]))
        frames = self.frames(ev["body"]["threadId"])
        if not frames:
            raise Fail("a stop with no frames")
        if line is not None and frames[0]["line"] != line:
            raise Fail("stopped at line %d, expected %d (%s)" % (frames[0]["line"], line, frames))
        return frames

    def frames(self, thread_id):
        return self.request("stackTrace", {"threadId": thread_id})["body"]["stackFrames"]

    def threads(self):
        return {t["id"]: t["name"] for t in self.request("threads")["body"]["threads"]}

    def wait_output(self, pred, what, timeout=60):
        deadline = time.time() + timeout
        with self.cv:
            while not pred(self.output):
                if self.closed:
                    raise Fail("adapter closed the channel while waiting for %s" % what)
                left = deadline - time.time()
                if left <= 0:
                    raise Fail("timed out waiting for %s; stdout %r"
                               % (what, self.output["stdout"][-300:]))
                self.cv.wait(left)

    def variables(self, frame_id, scope):
        scopes = self.request("scopes", {"frameId": frame_id})["body"]["scopes"]
        ref = [s for s in scopes if s["name"] == scope][0]["variablesReference"]
        vs = self.request("variables", {"variablesReference": ref})["body"]["variables"]
        return {v["name"]: v["value"] for v in vs}

    def evaluate(self, expr, frame_id, ok=True):
        r = self.request("evaluate", {"expression": expr, "frameId": frame_id,
                                      "context": "watch"}, ok=False)
        if ok and not r.get("success"):
            raise Fail("evaluate %r failed: %s" % (expr, r.get("message")))
        if not ok and r.get("success"):
            raise Fail("evaluate %r should have failed, answered %r" % (expr, r["body"]))
        return r["body"]["result"] if r.get("success") else r.get("message")

    def finish(self, exit_code=0):
        self.event("terminated")
        ex = self.event("exited")
        if exit_code == "nonzero":
            if ex["body"]["exitCode"] == 0:
                raise Fail("exit code 0, expected a failure")
        elif ex["body"]["exitCode"] != exit_code:
            raise Fail("exit code %r, expected %r; stderr output: %r"
                       % (ex["body"]["exitCode"], exit_code, self.output.get("stderr")))
        self.request("disconnect")
        self.close()

    def close(self):
        try:
            rc = self.p.wait(timeout=15)
        except subprocess.TimeoutExpired:
            self.p.kill()
            raise Fail("the adapter did not exit after disconnect")
        if rc != 0:
            raise Fail("the adapter exited %d; stderr: %r" % (rc, self.stderr[-400:]))


def expect(cond, what):
    if not cond:
        raise Fail(what)


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


SESSION = """\
// A fixture for the debugger: line numbers matter.
def fact(n: Int): Int =
  if n <= 1 then 1
  else n * fact(n - 1)

def twice(x: Int): Int =
  val y = x * 2
  y

val xs = List(1, 2, 3)
println("before")
val r = fact(3)
val t = twice(r)
println("after " + t)
"""


def scenario_session(exe, work):
    prog = os.path.join(work, "session.scala")
    write(prog, SESSION)
    d = Dap(exe, work)
    d.start(prog, {prog: [3, 13]})
    frames = d.stopped("breakpoint", 3)
    expect(frames[0]["name"] == "fact", "top frame %r" % frames[0])
    expect(os.path.normcase(frames[0]["source"]["path"]) == os.path.normcase(os.path.abspath(prog)),
           "frame source %r" % frames[0]["source"])
    expect(frames[1]["name"] == "<top>" and frames[1]["line"] == 12, "caller frame %r" % frames[1])
    top = frames[0]["id"]
    expect(d.variables(top, "Locals") == {"n": "3"}, "locals %r" % d.variables(top, "Locals"))
    g = d.variables(top, "Globals")
    expect(g.get("xs") == "List(1, 2, 3)" and "r" not in g, "globals %r" % g)
    expect(d.evaluate("n", top) == "3", "evaluate n")
    expect(d.evaluate("n * 10", top) == "30", "evaluate n * 10")
    expect(d.evaluate("xs.length + n", top) == "6", "evaluate xs.length + n")
    expect(d.evaluate('"s" + n', top) == '"s3"', "evaluate a String")
    expect(d.evaluate("fact(n + 1)", top) == "24", "evaluate a call")
    d.evaluate("nope + 1", top, ok=False)
    d.evaluate("n +", top, ok=False)
    # The caller's frame has no locals; its globals are the same.
    expect(d.evaluate("xs.head", frames[1]["id"]) == "1", "evaluate in the caller")

    d.set_breakpoints(prog, [13])          # line 3 no longer stops
    d.request("next", {"threadId": 1})
    frames = d.stopped("step", 4)
    expect(len(frames) == 2 and frames[0]["name"] == "fact", "next stays in the frame %r" % frames)
    d.request("stepIn", {"threadId": 1})
    frames = d.stopped("step", 3)
    expect(len(frames) == 3, "stepIn enters the recursive call %r" % frames)
    expect(d.variables(frames[0]["id"], "Locals") == {"n": "2"}, "inner locals")
    d.request("stepOut", {"threadId": 1})
    frames = d.stopped("step", 4)
    expect(len(frames) == 2 and frames[0]["name"] == "fact", "stepOut returns to the caller %r" % frames)
    d.request("stepOut", {"threadId": 1})
    frames = d.stopped("step", 12)          # back at the call site
    expect(len(frames) == 1 and frames[0]["name"] == "<top>", "stepOut to the top level %r" % frames)
    d.request("next", {"threadId": 1})
    frames = d.stopped("step", 13)
    expect(d.variables(frames[0]["id"], "Globals").get("r") == "6", "r after fact(3)")
    d.request("stepIn", {"threadId": 1})
    frames = d.stopped("step", 7)
    expect(frames[0]["name"] == "twice", "stepIn into twice %r" % frames)
    d.request("next", {"threadId": 1})
    frames = d.stopped("step", 8)
    expect(d.variables(frames[0]["id"], "Locals") == {"x": "6", "y": "12"},
           "twice's locals %r" % d.variables(frames[0]["id"], "Locals"))
    d.request("continue", {"threadId": 1})
    d.finish()
    expect(d.output["stdout"] == "before\nafter 12\n", "stdout %r" % d.output["stdout"])


def scenario_entry(exe, work):
    prog = os.path.join(work, "entry.scala")
    write(prog, SESSION)
    d = Dap(exe, work)
    d.start(prog, stopOnEntry=True)
    frames = d.stopped("entry")
    expect(frames[0]["name"] == "<top>" and frames[0]["line"] == 2, "entry frame %r" % frames[0])
    d.request("continue", {"threadId": 1})
    d.finish()


def scenario_output(exe, work):
    prog = os.path.join(work, "output.scala")
    write(prog, """\
println("before")
var i = 0
while i < 5000 do
  println("ñandú línea")
  i += 1
Console.err.println("to stderr")
val line = StdIn.readLine()
println("read " + line)
println("after")
""")
    d = Dap(exe, work)
    d.start(prog, {prog: [6]})
    d.stopped("breakpoint", 6)
    expect(d.output["stdout"].startswith("before\n"), "output before the stop")
    d.request("continue", {"threadId": 1})
    d.finish()
    lines = d.output["stdout"].splitlines()
    expect(lines[0] == "before" and lines.count("ñandú línea") == 5000, "stdout lines")
    expect(lines[-2:] == ["read null", "after"], "stdin at end of file: %r" % lines[-2:])
    expect("to stderr" in d.output["stderr"], "stderr %r" % d.output["stderr"])


def scenario_resolve(exe, work):
    prog = os.path.join(work, "resolve.scala")
    write(prog, SESSION)
    d = Dap(exe, work)
    d.request("initialize", {"adapterID": "protoscala"})
    d.event("initialized")
    d.request("launch", {"program": client_path(prog)})
    bps = d.set_breakpoints(prog, [5])     # a blank line before `def twice`
    expect(len(bps) == 1 and not bps[0]["verified"], "unverified before compiling %r" % bps)
    d.request("configurationDone")
    ev = d.event("breakpoint")
    b = ev["body"]["breakpoint"]
    expect(b["id"] == bps[0]["id"] and b["verified"] and b["line"] > 5,
           "resolved breakpoint %r" % b)
    d.stopped("breakpoint", b["line"])
    d.request("continue", {"threadId": 1})
    d.finish()


def scenario_module(exe, work):
    mod = os.path.join(work, "util", "Strings.scala")
    write(mod, """\
def shout(s: String): String =
  val up = s.toUpperCase
  up + "!"
""")
    prog = os.path.join(work, "usemodule.scala")
    write(prog, """\
import util.Strings
println(Strings.shout("hi"))
""")
    d = Dap(exe, work)
    d.start(prog, {mod: [3]})
    frames = d.stopped("breakpoint", 3)
    expect(frames[0]["name"] == "shout", "module frame %r" % frames[0])
    expect(os.path.normcase(frames[0]["source"]["path"]) == os.path.normcase(os.path.abspath(mod)),
           "module source %r" % frames[0]["source"])
    # A module's functions are methods of its object: `this` is the object.
    locals_ = d.variables(frames[0]["id"], "Locals")
    expect("this" in locals_ and {k: v for k, v in locals_.items() if k != "this"} ==
           {"s": '"hi"', "up": '"HI"'}, "module locals %r" % locals_)
    d.request("continue", {"threadId": 1})
    d.finish()
    expect(d.output["stdout"] == "HI!\n", "stdout %r" % d.output["stdout"])


def scenario_pause(exe, work):
    prog = os.path.join(work, "loop.scala")
    write(prog, """\
var i = 0
while true do
  i += 1
""")
    d = Dap(exe, work)
    d.start(prog)
    time.sleep(0.5)
    d.request("pause", {"threadId": 1})
    frames = d.stopped("pause")
    expect(frames[0]["line"] in (2, 3), "paused in the loop %r" % frames[0])
    d.request("disconnect")
    d.close()


EXCEPTIONS = """\
// A fixture for the exception filters: line numbers matter.
def risky(n: Int): Int =
  val k = n * 2
  if k > 4 then throw new IllegalStateException("too big: " + k)
  k

def outer(n: Int): Int =
  try risky(n)
  catch case e: IllegalArgumentException => 0
  finally println("cleanup")

val r =
  try outer(3)
  catch case e: IllegalStateException => -1
println("caught " + r)
val zero = r + 1
val q =
  try 10 / zero
  catch case e: ArithmeticException => 7
println("q " + q)
"""


def exception_info(d, tid):
    return d.request("exceptionInfo", {"threadId": tid})["body"]


def scenario_raised(exe, work):
    prog = os.path.join(work, "raised.scala")
    write(prog, EXCEPTIONS)
    d = Dap(exe, work)
    d.start(prog, exceptions=["raised"])
    filters = {f["filter"] for f in d.capabilities.get("exceptionBreakpointFilters", [])}
    expect({"raised", "userUnhandled"} <= filters, "advertised filters %r" % filters)
    expect(d.capabilities.get("supportsExceptionInfoRequest"), "exceptionInfo not advertised")
    frames = d.stopped("exception", 4)
    tid = d.last_stop["threadId"]
    expect("IllegalStateException" in d.last_stop.get("text", ""), "stop text %r" % d.last_stop)
    expect([f["name"] for f in frames] == ["risky", "outer", "<top>"], "stack %r" % frames)
    expect(frames[1]["line"] == 8 and frames[2]["line"] == 13, "callers %r" % frames)
    expect(d.variables(frames[0]["id"], "Locals") == {"n": "3", "k": "6"},
           "locals at the throw %r" % d.variables(frames[0]["id"], "Locals"))
    expect(d.evaluate("k + n", frames[0]["id"]) == "9", "evaluate at the throw")
    info = exception_info(d, tid)
    expect(info["exceptionId"] == "IllegalStateException" and info["breakMode"] == "always" and
           info["details"]["message"] == "too big: 6" and "risky" in info["details"]["stackTrace"],
           "exceptionInfo %r" % info)
    # The catch in `outer` does not match and re-raises, its finally re-raises
    # again: the same exception, which must not stop twice.
    d.request("continue", {"threadId": tid})
    frames = d.stopped("exception", 18)    # 10 / zero: raised by the runtime
    info = exception_info(d, d.last_stop["threadId"])
    expect(info["exceptionId"] == "ArithmeticException", "runtime error %r" % info)
    d.request("continue", {"threadId": d.last_stop["threadId"]})
    d.finish()
    expect(d.output["stdout"] == "cleanup\ncaught -1\nq 7\n", "stdout %r" % d.output["stdout"])


INTERNAL = """\
var total = 0
for x <- List(1, 2, 3) do total += x
val it = new __ListIterator(List(4, 5))
while it.hasNext do total += it.next()
total += List(1, 2).map(x => x * 2).sum + Some(1).getOrElse(0)
total += Try(10).getOrElse(0)
val echo = Actor.spawn(0) { (s, m) => (s, m) }
val caller = Actor.spawn(0) { (s, m) =>
  var acc = 0
  var i = 1
  while i <= 3 do
    acc += (echo ? i).await
    i += 1
  (acc, acc)
}
total += (caller ? 0).await + Future { 2 }.await
val last =
  try throw new RuntimeException("the one")
  catch case e: RuntimeException => 1
println("total " + (total + last))
"""


def scenario_internal(exe, work):
    prog = os.path.join(work, "internal.scala")
    write(prog, INTERNAL)
    d = Dap(exe, work)
    d.start(prog, exceptions=["raised"])
    d.stopped("exception", 18)
    info = exception_info(d, d.last_stop["threadId"])
    expect(info["exceptionId"] == "RuntimeException", "the only stop %r" % info)
    d.request("continue", {"threadId": d.last_stop["threadId"]})
    d.finish()
    expect(d.output["stdout"] == "total 41\n", "stdout %r" % d.output["stdout"])


def scenario_nofilter(exe, work):
    prog = os.path.join(work, "nofilter.scala")
    write(prog, EXCEPTIONS)
    d = Dap(exe, work)
    d.start(prog, exceptions=[])
    d.finish()
    expect(d.output["stdout"] == "cleanup\ncaught -1\nq 7\n", "stdout %r" % d.output["stdout"])


UNCAUGHT = """\
def risky(n: Int): Int =
  val k = n * 2
  if k > 4 then throw new IllegalStateException("too big: " + k)
  k

val a =
  try risky(3)
  catch case e: IllegalStateException => -1
val b = Try(risky(5)).isFailure
val c =
  try risky(4)
  catch case e: IOException => -2
println("never")
"""


def scenario_uncaught(exe, work):
    prog = os.path.join(work, "uncaught.scala")
    write(prog, UNCAUGHT)
    d = Dap(exe, work)
    d.start(prog, exceptions=["userUnhandled"])
    frames = d.stopped("exception", 3)
    tid = d.last_stop["threadId"]
    expect([(f["name"], f["line"]) for f in frames] == [("risky", 3), ("<top>", 11)],
           "stack at the uncaught throw %r" % frames)
    expect(d.variables(frames[0]["id"], "Locals") == {"n": "4", "k": "8"}, "locals")
    g = d.variables(frames[0]["id"], "Globals")
    expect(g.get("a") == "-1" and g.get("b") == "true", "globals %r" % g)
    info = exception_info(d, tid)
    expect(info["breakMode"] == "userUnhandled" and info["exceptionId"] == "IllegalStateException",
           "exceptionInfo %r" % info)
    d.request("continue", {"threadId": tid})
    d.finish("nonzero")
    expect("never" not in d.output["stdout"], "stdout %r" % d.output["stdout"])
    expect("too big: 8" in d.output["stderr"], "stderr %r" % d.output["stderr"])


THREADS = """\
def compute(tag: String, n: Int): Int =
  var acc = 0
  var i = 0
  while i < n do
    acc += i + 1
    i += 1
  acc

def busyUntil(deadline: Long): Unit =
  while System.nanoTime() < deadline do ()

val t1 = Thread.start(() => println("a done " + compute("a", 2)))
val t2 = Thread.start(() => println("b done " + compute("b", 2)))
val ticker = Thread.start { () =>
  var k = 0
  while k < 150 do
    println("tick " + k)
    busyUntil(System.nanoTime() + 20000000L)
    k += 1
}
t1.join()
t2.join()
ticker.join()
println("all done")
"""


def scenario_threads(exe, work):
    prog = os.path.join(work, "threads.scala")
    write(prog, THREADS)
    d = Dap(exe, work)
    d.start(prog, {prog: [5]})
    d.stopped("breakpoint", 5)
    first = d.last_stop
    d.stopped("breakpoint", 5)
    second = d.last_stop
    ids = {first["threadId"], second["threadId"]}
    expect(len(ids) == 2 and not first["allThreadsStopped"], "two threads stopped %r %r"
           % (first, second))
    names = d.threads()
    expect(names.get(1) == "main" and all(names.get(t, "").startswith("Thread-") for t in ids),
           "thread names %r" % names)
    tags = {}
    for t in ids:
        fs = d.frames(t)
        expect(fs[0]["name"] == "compute", "frames of %d: %r" % (t, fs))
        tags[d.variables(fs[0]["id"], "Locals")["tag"]] = t
    expect(set(tags) == {'"a"', '"b"'}, "one stop per thread %r" % tags)
    ta, tb = tags['"a"'], tags['"b"']
    # Both stopped: the ticker still prints.
    ticks = d.output["stdout"].count("tick ")
    d.wait_output(lambda o: o["stdout"].count("tick ") >= ticks + 3, "ticks while stopped")
    # A step in `a` leaves `b` where it is.
    d.set_breakpoints(prog, [])
    d.request("next", {"threadId": ta})
    ev = d.event("stopped")
    expect(ev["body"]["threadId"] == ta and ev["body"]["reason"] == "step", "step stop %r" % ev)
    fa = d.frames(ta)
    expect(fa[0]["line"] == 6, "a after next %r" % fa[0])
    fb = d.frames(tb)
    expect(fb and fb[0]["line"] == 5, "b still stopped %r" % fb)
    expect(d.variables(fb[0]["id"], "Locals").get("tag") == '"b"', "b's locals")
    expect(d.evaluate("acc + n", fb[0]["id"]) == "2", "evaluate in b")
    r = d.request("continue", {"threadId": ta})
    expect(r["body"].get("allThreadsContinued") is False, "continue body %r" % r["body"])
    d.wait_output(lambda o: "a done 3" in o["stdout"], "a to finish")
    expect("b done" not in d.output["stdout"], "b ran while stopped")
    d.request("continue", {"threadId": tb})
    d.finish()
    out = d.output["stdout"]
    expect("b done 3" in out and out.endswith("all done\n"), "stdout %r" % out[-200:])


PAUSE_THREAD = """\
def spin(tag: String, ms: Long): Int =
  val deadline = System.nanoTime() + ms * 1000000L
  var n = 0
  while System.nanoTime() < deadline do
    n += 1
  println(tag + " finished")
  n

val a = Thread.start(() => spin("a", 1500))
val b = Thread.start { () =>
  var k = 0
  while k < 120 do
    spin("b" + k, 25)
    k += 1
}
a.join()
b.join()
println("all done")
"""


def scenario_pausethread(exe, work):
    prog = os.path.join(work, "pausethread.scala")
    write(prog, PAUSE_THREAD)
    d = Dap(exe, work)
    d.start(prog)
    d.wait_output(lambda o: "b2 finished" in o["stdout"], "b to run")
    names = d.threads()
    ta = [t for t, n in names.items() if n == "Thread-1"]
    expect(ta, "no Thread-1 in %r" % names)
    d.request("pause", {"threadId": ta[0]})
    frames = d.stopped("pause")
    expect(d.last_stop["threadId"] == ta[0], "paused thread %r" % d.last_stop)
    expect(frames[0]["name"] == "spin" and
           d.variables(frames[0]["id"], "Locals").get("tag") == '"a"', "paused in a %r" % frames)
    done = d.output["stdout"].count(" finished")
    d.wait_output(lambda o: o["stdout"].count(" finished") >= done + 3, "b while a is paused")
    expect("a finished" not in d.output["stdout"], "a ran while paused")
    d.request("continue", {"threadId": ta[0]})
    d.finish()
    expect(d.output["stdout"].endswith("all done\n") and "a finished" in d.output["stdout"],
           "stdout %r" % d.output["stdout"][-200:])


GC = """\
def oneRound(i: Int): Int =
  var xs = List[Int]()
  var j = 0
  while j < 50 do
    xs = (i + j) :: xs
    j += 1
  xs.length

def churn(rounds: Int): Int =
  var made = 0
  var i = 0
  while i < rounds do
    made += oneRound(i)
    i += 1
  made

def hold(): Int =
  var keep = List[Int]()
  var j = 0
  while j < 200 do
    keep = (j * 3) :: keep
    j += 1
  val label = "kept " + keep.length
  label.length + keep.sum

val worker = Thread.start(() => println("churned " + churn(ROUNDS)))
val r = hold()
worker.join()
println("held " + r)
"""


def scenario_gc(exe, work):
    rounds = 30000
    prog = os.path.join(work, "gc.scala")
    write(prog, GC.replace("ROUNDS", str(rounds)))
    d = Dap(exe, work, env={"PROTOCORE_HEAP_TRACE": "1", "PROTOCORE_HEAP_LIMIT_CELLS": "1000000"})
    d.start(prog, {prog: [24]})
    frames = d.stopped("breakpoint", 24)
    tid = d.last_stop["threadId"]
    expect(tid == 1 and frames[0]["name"] == "hold", "main stopped in hold %r" % frames)
    before = d.output["stderr"].count("protoCore heap:")
    d.wait_output(lambda o: "churned " in o["stdout"], "the worker's churn", timeout=240)
    cycles = d.output["stderr"].count("protoCore heap:") - before
    expect(cycles >= 3, "only %d collections while stopped" % cycles)
    loc = d.variables(frames[0]["id"], "Locals")
    expect(loc.get("label") == '"kept 200"' and loc.get("j") == "200" and
           loc.get("keep", "").startswith("List(597, 594, 591"), "locals after GC %r" % loc)
    expect(d.evaluate("keep.sum + keep.length", frames[0]["id"]) == "59900", "evaluate after GC")
    d.request("continue", {"threadId": tid})
    d.finish()
    expect("churned %d" % (rounds * 50) in d.output["stdout"] and
           d.output["stdout"].endswith("held 59708\n"), "stdout %r" % d.output["stdout"])


UNCAUGHT_THREADS = """\
def boom(where: String): Int =
  throw new IllegalStateException("boom in " + where)

val f = Future { boom("future") }
val v = try f.await catch case e: IllegalStateException => -1
val t = Thread.start(() => boom("thread"))
t.join()
val actor = Actor.spawn(0) { (s, m) => (s, boom("actor")) }
val w = try (actor ? 1).await catch case e: IllegalStateException => -2
println("end " + v + " " + w)
"""


def scenario_uncaughtthreads(exe, work):
    prog = os.path.join(work, "uncaughtthreads.scala")
    write(prog, UNCAUGHT_THREADS)
    d = Dap(exe, work)
    d.start(prog, exceptions=["userUnhandled"])
    # A Future's body fails its future (no stop); a thread body's and an actor
    # handler's exception is the end of the line for that thread: both stop.
    frames = d.stopped("exception", 2)
    tid = d.last_stop["threadId"]
    names = d.threads()
    expect(names.get(tid) == "Thread-1" and [f["name"] for f in frames] == ["boom", "<lambda>"],
           "thread stop %r %r" % (names.get(tid), frames))
    expect(d.variables(frames[0]["id"], "Locals") == {"where": '"thread"'}, "thread locals")
    started = d.wait_for(lambda m: m.get("event") == "thread" and
                         m["body"] == {"reason": "started", "threadId": tid}, "thread started")
    expect(started, "no thread event")
    d.request("continue", {"threadId": tid})
    d.wait_for(lambda m: m.get("event") == "thread" and
               m["body"] == {"reason": "exited", "threadId": tid}, "thread exited")
    frames = d.stopped("exception", 2)
    aid = d.last_stop["threadId"]
    name = d.threads().get(aid, "")
    expect(aid not in (1, tid) and name.startswith("actor worker") and "actor#" in name,
           "actor stop on %r" % name)
    expect(d.variables(frames[0]["id"], "Locals") == {"where": '"actor"'}, "actor locals")
    d.request("continue", {"threadId": aid})
    d.finish()
    expect(d.output["stdout"] == "end -1 -2\n", "stdout %r" % d.output["stdout"])



CONDITIONS = """\
def check(n: Int): Boolean =
  n > 1
def risky(n: Int): Int =
  if n > 0 then throw new IllegalStateException("bad " + n)
  n
var i = 0
var sum = 0
while i < 4 do
  sum += (try risky(i) catch case e: IllegalStateException => 10)
  i += 1
println("sum " + sum)
"""


def raised_with(d, prog, condition, breakpoints=None):
    d.request("initialize", {"adapterID": "protoscala"})
    d.event("initialized")
    d.request("launch", {"program": client_path(prog)})
    for path, lines in (breakpoints or {}).items():
        d.set_breakpoints(path, lines)
    r = d.request("setExceptionBreakpoints",
                  {"filters": [], "filterOptions": [{"filterId": "raised", "condition": condition}]})
    d.request("configurationDone")
    return r["body"]["breakpoints"]


def scenario_exccondition(exe, work):
    prog = os.path.join(work, "conditions.scala")
    write(prog, CONDITIONS)
    # True only for the second raise. The condition calls `check`, whose line
    # has a breakpoint: evaluation never stops there.
    d = Dap(exe, work)
    caps = d.request("initialize", {"adapterID": "protoscala"})["body"]
    expect(caps.get("supportsExceptionFilterOptions") and
           all(f.get("supportsCondition") and "`e`" in f.get("conditionDescription", "")
               for f in caps["exceptionBreakpointFilters"]), "condition capabilities %r" % caps)
    d.event("initialized")
    d.request("launch", {"program": client_path(prog)})
    d.set_breakpoints(prog, [2])
    d.request("setExceptionBreakpoints", {"filterOptions": [
        {"filterId": "raised", "condition": 'check(n) && e.getMessage == "bad 2"'}]})
    d.request("configurationDone")
    frames = d.stopped("exception", 4)
    expect(d.variables(frames[0]["id"], "Locals") == {"n": "2"}, "stopped at n == 2")
    d.request("continue", {"threadId": d.last_stop["threadId"]})
    d.finish()
    expect(d.output["stdout"] == "sum 30\n", "stdout %r" % d.output["stdout"])
    # Always false: no stop.
    d = Dap(exe, work)
    raised_with(d, prog, 'e.getMessage == "nope"')
    d.finish()
    expect(d.output["stdout"] == "sum 30\n", "stdout %r" % d.output["stdout"])
    # A condition that throws stops, and says why.
    d = Dap(exe, work)
    raised_with(d, prog, "e.getMessage.length / 0 > 1")
    frames = d.stopped("exception", 4)
    desc = d.last_stop.get("description", "")
    expect("condition" in desc and "failed" in desc and "ArithmeticException" in desc,
           "condition error in the stop %r" % d.last_stop)
    expect("exception breakpoint condition" in d.output["stderr"], "stderr %r" % d.output["stderr"])
    d.request("setExceptionBreakpoints", {"filters": []})
    d.request("continue", {"threadId": d.last_stop["threadId"]})
    d.finish()
    # A syntax error is reported when it is set, and stops where it is used.
    d = Dap(exe, work)
    bps = raised_with(d, prog, "e.getMessage ==")
    expect("failed" in bps[0].get("message", ""), "set-time report %r" % bps)
    d.wait_output(lambda o: "exception breakpoint condition" in o["stderr"], "set-time output")
    d.stopped("exception", 4)
    expect("failed" in d.last_stop.get("description", ""), "syntax error stop %r" % d.last_stop)
    d.request("setExceptionBreakpoints", {"filters": []})
    d.request("continue", {"threadId": d.last_stop["threadId"]})
    d.finish()


LOOP = """\
var total = 0
var i = 0
while i < 6 do
  total += i
  i += 1
println("total " + total)
"""


def stops_with(exe, work, bp):
    """The values of `i` at each stop on line 4 with breakpoint `bp`."""
    prog = os.path.join(work, "loop.scala")
    write(prog, LOOP)
    d = Dap(exe, work)
    d.request("initialize", {"adapterID": "protoscala"})
    d.event("initialized")
    d.request("launch", {"program": client_path(prog)})
    b = dict(bp)
    b["line"] = 4
    answered = d.request("setBreakpoints", {"source": {"path": client_path(prog)},
                                            "breakpoints": [b]})["body"]["breakpoints"]
    d.request("configurationDone")
    seen = []
    notes = []
    while True:
        ev = d.wait_for(lambda m: m.get("event") in ("stopped", "terminated"), "a stop or the end")
        if ev["event"] == "terminated":
            break
        tid = ev["body"]["threadId"]
        fs = d.frames(tid)
        seen.append(int(d.variables(fs[0]["id"], "Locals").get("i", d.variables(
            fs[0]["id"], "Globals").get("i"))))
        notes.append(ev["body"].get("description", ""))
        d.request("continue", {"threadId": tid})
    d.event("exited")
    d.request("disconnect")
    d.close()
    expect(d.output["stdout"].endswith("total 15\n"), "stdout %r" % d.output["stdout"])
    return seen, notes, answered, d.output


def scenario_linebp(exe, work):
    seen, _, _, _ = stops_with(exe, work, {"condition": "i == 3"})
    expect(seen == [3], "condition i == 3 stopped at %r" % seen)
    seen, _, _, _ = stops_with(exe, work, {"condition": "i == 99"})
    expect(seen == [], "condition false stopped at %r" % seen)
    for hit, want in (("3", [2]), ("== 3", [2]), (">= 5", [4, 5]), ("> 4", [4, 5]),
                      ("< 3", [0, 1]), ("<= 1", [0]), ("% 2", [1, 3, 5])):
        seen, _, _, _ = stops_with(exe, work, {"hitCondition": hit})
        expect(seen == want, "hitCondition %r stopped at %r, expected %r" % (hit, seen, want))
    # Only the hits whose condition held count.
    seen, _, _, _ = stops_with(exe, work, {"condition": "i % 2 == 0", "hitCondition": "2"})
    expect(seen == [2], "condition + hitCondition stopped at %r" % seen)
    seen, _, answered, _ = stops_with(exe, work, {"hitCondition": "every other"})
    expect(not answered[0]["verified"] and "hit condition" in answered[0].get("message", "") and
           seen == [], "invalid hitCondition %r %r" % (answered, seen))
    # A condition that fails stops, and says why.
    seen, notes, _, out = stops_with(exe, work, {"condition": "i / 0 == 1"})
    expect(len(seen) == 6 and "failed" in notes[0] and "breakpoint condition" in out["stderr"],
           "failing condition %r %r" % (seen, notes))


def scenario_logpoint(exe, work):
    seen, _, _, out = stops_with(exe, work, {"logMessage": "i={i} total={total} {{literal}} s={\"x\"}"})
    expect(seen == [], "a logpoint stopped %r" % seen)
    lines = [l for l in out.get("console", "").splitlines() if l.startswith("i=")]
    expect(lines == ["i=%d total=%d {literal} s=x" % (k, sum(range(k))) for k in range(6)],
           "logpoint output %r" % lines)
    seen, _, _, out = stops_with(exe, work, {"logMessage": "bad {nope + 1}", "hitCondition": "% 3"})
    lines = [l for l in out.get("console", "").splitlines() if l.startswith("bad")]
    expect(seen == [] and len(lines) == 2 and all(l.startswith("bad <error: ") for l in lines),
           "logpoint error %r" % lines)


CONDITION_THREADS = """\
def compute(tag: String, n: Int): Int =
  var acc = 0
  var i = 0
  while i < n do
    acc += i + 1
    i += 1
  acc

val t1 = Thread.start(() => println("a done " + compute("a", 3)))
val t2 = Thread.start(() => println("b done " + compute("b", 3)))
t1.join()
t2.join()
println("all done")
"""


def scenario_condthreads(exe, work):
    prog = os.path.join(work, "condthreads.scala")
    write(prog, CONDITION_THREADS)
    d = Dap(exe, work)
    d.request("initialize", {"adapterID": "protoscala"})
    d.event("initialized")
    d.request("launch", {"program": client_path(prog)})
    d.request("setBreakpoints", {"source": {"path": client_path(prog)},
                                 "breakpoints": [{"line": 5, "condition": 'tag == "b" && i == 1'}]})
    d.request("configurationDone")
    frames = d.stopped("breakpoint", 5)
    loc = d.variables(frames[0]["id"], "Locals")
    expect(loc.get("tag") == '"b"' and loc.get("i") == "1", "the b thread at i == 1 %r" % loc)
    d.wait_output(lambda o: "a done 6" in o["stdout"], "a to finish while b is stopped")
    d.request("continue", {"threadId": d.last_stop["threadId"]})
    d.finish()
    expect(d.output["stdout"].endswith("all done\n") and "b done 6" in d.output["stdout"],
           "stdout %r" % d.output["stdout"])



PRELUDE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib", "prelude.scala")

EMBEDDED = """\
def check(n: Int): Int =
  require(n > 0, "positive")
  n + 1
println(check(1))
println(check(2))
"""


def scenario_embedded(exe, work):
    prog = os.path.join(work, "embedded.scala")
    write(prog, EMBEDDED)
    d = Dap(exe, work)
    caps = d.request("initialize", {"adapterID": "protoscala"})["body"]
    expect(caps.get("supportsLoadedSourcesRequest"), "loadedSources not advertised")
    d.event("initialized")
    d.request("launch", {"program": client_path(prog)})
    d.set_breakpoints(prog, [2])
    d.request("configurationDone")
    d.stopped("breakpoint", 2)
    tid = d.last_stop["threadId"]
    # Into the prelude's `require`: a frame whose source is the embedded text.
    d.request("stepIn", {"threadId": tid})
    frames = d.stopped("step")
    src = frames[0]["source"]
    expect(frames[0]["name"] == "require" and src.get("sourceReference", 0) > 0 and
           "path" not in src and src.get("presentationHint") == "deemphasize" and
           src.get("name") == "prelude.scala", "a prelude frame %r" % frames[0])
    expect(frames[1]["name"] == "check" and frames[1]["line"] == 2, "its caller %r" % frames[1])
    ref = src["sourceReference"]
    body = d.request("source", {"sourceReference": ref, "source": src})["body"]
    with open(PRELUDE, encoding="utf-8") as f:
        expect(body["content"] == f.read(), "the prelude's text")
    line = frames[0]["line"]
    expect("def require" in body["content"].splitlines()[line - 2], "the line shown %d" % line)
    # A breakpoint in the prelude, by sourceReference.
    d.set_breakpoints(prog, [])
    r = d.request("setBreakpoints", {"source": {"name": "prelude.scala", "sourceReference": ref},
                                     "breakpoints": [{"line": line}]})["body"]["breakpoints"]
    expect(r[0]["verified"] and r[0]["source"]["sourceReference"] == ref, "prelude breakpoint %r" % r)
    d.request("continue", {"threadId": tid})
    frames = d.stopped("breakpoint", line)
    expect(frames[0]["source"].get("sourceReference") == ref and
           d.variables(frames[1]["id"], "Locals") == {"n": "2"}, "stopped in require for check(2)")
    names = [s["name"] for s in d.request("loadedSources")["body"]["sources"]]
    expect("prelude.scala" in names and "embedded.scala" in names, "loaded sources %r" % names)
    d.request("setBreakpoints", {"source": {"sourceReference": ref}, "breakpoints": []})
    d.request("continue", {"threadId": tid})
    d.finish()
    expect(d.output["stdout"] == "2\n3\n", "stdout %r" % d.output["stdout"])
    # justMyCode: a step into the prelude stays in the program.
    d = Dap(exe, work)
    d.start(prog, {prog: [2]}, justMyCode=True)
    d.stopped("breakpoint", 2)
    d.request("stepIn", {"threadId": d.last_stop["threadId"]})
    frames = d.stopped("step", 3)
    expect(frames[0]["name"] == "check", "justMyCode step %r" % frames[0])
    d.set_breakpoints(prog, [])
    d.request("continue", {"threadId": d.last_stop["threadId"]})
    d.finish()


def scenario_pathmap(exe, work):
    proj = os.path.join(work, "proj")
    lib = os.path.join(work, "lib")
    main = os.path.join(proj, "main.scala")
    mod = os.path.join(lib, "util", "Strings.scala")
    write(main, """\
import util.Strings
def twice(x: Int): Int =
  val y = x * 2
  y
println(twice(4))
println(Strings.shout("hi"))
""")
    write(mod, """\
def shout(s: String): String =
  val up = s.toUpperCase
  up + "!"
""")
    if os.name == "nt":
        fake = "c:\\Fake\\Proj"
        fake_main = "C:/fake/proj/main.scala"     # another spelling of the same path
        shown_main = "c:\\Fake\\Proj\\main.scala"
    else:
        fake = "/fake/proj"
        fake_main = "/fake/proj/main.scala"
        shown_main = fake_main
    # Relative: PROTOSCALA_PATH is split on ":" on every platform.
    env = {"PROTOSCALA_PATH": os.path.join("..", "lib")}
    # An entry without both roots is refused.
    d = Dap(exe, work, env=env)
    d.request("initialize", {"adapterID": "protoscala"})
    d.event("initialized")
    r = d.request("launch", {"program": fake_main, "pathMappings": [{"localRoot": fake}]}, ok=False)
    expect(not r["success"] and "remoteRoot" in r.get("message", ""), "bad mapping %r" % r)
    d.request("disconnect")
    d.close()

    d = Dap(exe, work, env=env)
    d.request("initialize", {"adapterID": "protoscala"})
    d.event("initialized")
    d.request("launch", {"program": fake_main, "cwd": fake,
                         "pathMappings": [{"localRoot": fake, "remoteRoot": proj}]})
    bps = d.request("setBreakpoints", {"source": {"path": fake_main},
                                       "breakpoints": [{"line": 3}]})["body"]["breakpoints"]
    expect(len(bps) == 1, "breakpoints %r" % bps)
    d.request("configurationDone")
    frames = d.stopped("breakpoint", 3)
    tid = d.last_stop["threadId"]
    expect(frames[0]["source"].get("path") == shown_main and
           frames[1]["source"].get("path") == shown_main, "client paths %r" % frames)
    loaded = {s["name"]: s for s in d.request("loadedSources")["body"]["sources"]}
    expect(loaded.get("main.scala", {}).get("path") == shown_main, "main in loaded %r" % loaded)
    strings = loaded.get("Strings.scala", {})
    expect(strings.get("sourceReference", 0) > 0 and "path" not in strings and
           os.path.normcase(os.path.abspath(mod)) in os.path.normcase(strings.get("origin", "")),
           "a module outside the mapping %r" % strings)
    content = d.request("source", {"sourceReference": strings["sourceReference"]})["body"]["content"]
    with open(mod, encoding="utf-8") as f:
        expect(content == f.read(), "its text")
    # A breakpoint on it by reference.
    r = d.request("setBreakpoints", {"source": {"name": "Strings.scala",
                                                "sourceReference": strings["sourceReference"]},
                                     "breakpoints": [{"line": 3}]})["body"]["breakpoints"]
    expect(r[0]["verified"], "breakpoint by reference %r" % r)
    d.request("continue", {"threadId": tid})
    frames = d.stopped("breakpoint", 3)
    expect(frames[0]["name"] == "shout" and
           frames[0]["source"].get("sourceReference") == strings["sourceReference"] and
           frames[1]["source"].get("path") == shown_main, "stopped in the module %r" % frames)
    # `source` by a client path reads through the mapping.
    content = d.request("source", {"source": {"path": fake_main}, "sourceReference": 0})["body"]["content"]
    expect(content.startswith("import util.Strings"), "source by path %r" % content[:40])
    d.request("continue", {"threadId": tid})
    d.finish()
    expect(d.output["stdout"] == "8\nHI!\n", "stdout %r" % d.output["stdout"])


SCENARIOS = {
    "session": scenario_session,
    "entry": scenario_entry,
    "output": scenario_output,
    "resolve": scenario_resolve,
    "module": scenario_module,
    "pause": scenario_pause,
    "raised": scenario_raised,
    "internal": scenario_internal,
    "nofilter": scenario_nofilter,
    "uncaught": scenario_uncaught,
    "threads": scenario_threads,
    "pausethread": scenario_pausethread,
    "gc": scenario_gc,
    "uncaughtthreads": scenario_uncaughtthreads,
    "exccondition": scenario_exccondition,
    "linebp": scenario_linebp,
    "logpoint": scenario_logpoint,
    "condthreads": scenario_condthreads,
    "embedded": scenario_embedded,
    "pathmap": scenario_pathmap,
}


def main():
    exe = os.path.abspath(sys.argv[1])
    names = sys.argv[2:] or list(SCENARIOS)
    work = tempfile.mkdtemp(prefix="dap-session.", dir=os.getcwd())
    failed = 0
    try:
        for name in names:
            d = os.path.join(work, name)
            os.makedirs(d)
            try:
                SCENARIOS[name](exe, d)
                print("ok   %s" % name)
            except Fail as e:
                failed += 1
                print("FAIL %s: %s" % (name, e))
    finally:
        for p in ADAPTERS:
            if p.poll() is None:
                p.kill()
                p.wait()
        shutil.rmtree(work, ignore_errors=True)
    if failed:
        sys.exit(1)
    print("OK")


if __name__ == "__main__":
    main()
