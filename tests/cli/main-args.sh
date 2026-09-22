#!/usr/bin/env bash
#
# CLI check: arguments after the script path reach `@main def run(args: String*)`.
#
# Usage: main-args.sh <path-to-protoscala>
set -u
P="${1:?usage: main-args.sh <protoscala>}"
work=$(mktemp -d -p "$PWD" main-args.XXXXXX)
trap 'rm -rf "$work"' EXIT
printf '@main def run(args: String*): Unit =\n  println(args.length.toString + ":" + args.mkString("+"))\n' >"$work/args.scala"
out=$("$P" "$work/args.scala" a b c 2>&1); rc=$?
if [[ $rc -ne 0 || "$out" != "3:a+b+c" ]]; then
    echo "FAIL: exit $rc, output '$out' (expected '3:a+b+c')"
    exit 1
fi
echo OK
