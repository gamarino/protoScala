#!/usr/bin/env python3
r"""CLI check: PROTOSCALA_PATH is a list of absolute directories separated as
PATH is (os.pathsep: ';' on Windows, where an absolute path starts with a
drive letter and contains ':', and ':' elsewhere).

Usage: module_path_check.py <protoscala>

The module lives in the FIRST of two absolute entries. Split on ':' on
Windows, the list would give `C`, `\...\with-module;C` and `\...\empty`,
none of which holds it. (The last piece of such a split is a drive-relative
path that still resolves, so a module in the last entry would hide the bug.)
"""
import os
import shutil
import subprocess
import sys
import tempfile


def main():
    exe = os.path.abspath(sys.argv[1])
    work = tempfile.mkdtemp(prefix="module-path.", dir=os.getcwd())
    try:
        empty = os.path.join(work, "empty")
        with_module = os.path.join(work, "with-module", "nested")
        os.makedirs(empty)
        os.makedirs(os.path.join(with_module, "util"))
        with open(os.path.join(with_module, "util", "Greet.scala"), "w", newline="\n") as f:
            f.write('def hello(name: String): String = "hello " + name\n')
        script = os.path.join(work, "main.scala")
        with open(script, "w", newline="\n") as f:
            f.write('import util.Greet\nprintln(Greet.hello("path"))\n')
        env = dict(os.environ)
        env["PROTOSCALA_PATH"] = os.pathsep.join([os.path.abspath(with_module), os.path.abspath(empty)])
        # Run from elsewhere: the working directory is the last place searched.
        r = subprocess.run([exe, script], cwd=empty, env=env, capture_output=True, text=True,
                           timeout=120)
        if r.returncode != 0 or r.stdout != "hello path\n":
            print("FAIL: PROTOSCALA_PATH=%r rc=%d stdout=%r stderr=%r"
                  % (env["PROTOSCALA_PATH"], r.returncode, r.stdout, r.stderr))
            sys.exit(1)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print("OK")


if __name__ == "__main__":
    main()
