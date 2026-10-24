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
    def __init__(self, exe, cwd):
        self.p = subprocess.Popen([exe, "--dap"], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=cwd)
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

    def start(self, program, breakpoints=None, **launch):
        self.request("initialize", {"adapterID": "protoscala", "pathFormat": "path",
                                    "linesStartAt1": True})
        self.event("initialized")
        args = {"program": client_path(program)}
        args.update(launch)
        self.request("launch", args)
        for path, lines in (breakpoints or {}).items():
            self.set_breakpoints(path, lines)
        self.request("configurationDone")

    def set_breakpoints(self, path, lines):
        return self.request("setBreakpoints", {
            "source": {"path": client_path(path)},
            "breakpoints": [{"line": l} for l in lines]})["body"]["breakpoints"]

    def stopped(self, reason=None, line=None):
        ev = self.event("stopped")
        if reason and ev["body"]["reason"] != reason:
            raise Fail("stopped for %r, expected %r" % (ev["body"]["reason"], reason))
        frames = self.request("stackTrace", {"threadId": 1})["body"]["stackFrames"]
        if not frames:
            raise Fail("a stop with no frames")
        if line is not None and frames[0]["line"] != line:
            raise Fail("stopped at line %d, expected %d (%s)" % (frames[0]["line"], line, frames))
        return frames

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
        if ex["body"]["exitCode"] != exit_code:
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


SCENARIOS = {
    "session": scenario_session,
    "entry": scenario_entry,
    "output": scenario_output,
    "resolve": scenario_resolve,
    "module": scenario_module,
    "pause": scenario_pause,
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
