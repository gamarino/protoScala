#!/usr/bin/env bash
#
# CLI check: `protoscala --version` prints "protoScala X.Y.Z" and exits 0.
#
# Usage: version.sh <path-to-protoscala>
set -u
PROTOSCALA="${1:?usage: version.sh <protoscala>}"

out=$("$PROTOSCALA" --version 2>&1)
rc=$?
if [[ $rc -ne 0 ]]; then
    echo "FAIL: --version exited $rc"
    exit 1
fi
if ! grep -qE '^protoScala [0-9]+\.[0-9]+\.[0-9]+ \(actor mailboxes: .+\)$' <<<"$out"; then
    echo "FAIL: unexpected --version output: $out"
    exit 1
fi

# The prelude line must report what the binary actually DOES, and the oracle for
# "does" is the runtime itself, not another reading of the same code path.
#
# It did not, between 3fa7ff0 and this check: `--version` tested
# PROTOSCALA_HAVE_PRELUDE_IMAGE in src/main.cpp, that macro is PUBLIC on an OBJECT
# library, and once protoscala linked the shared protoScala target -- which links the
# object libraries PRIVATE -- it stopped reaching main.cpp. The image stayed in use and
# the line started denying it. A `--version` that is quietly wrong is worse than one
# that is missing, because it is what a user checks.
#
# GROUND TRUTH: PROTOSCALA_PRELUDE_TIMING prints `image=1` when loadPrelude took the
# precompiled-image path and `image=0` when it compiled the source. That flag is set
# by the code that actually chooses, so comparing `--version` against it catches the
# shipped bug head-on. A first attempt at this check compared `--version` with and
# without PROTOSCALA_PRELUDE_NO_IMAGE=1 and required the two lines to differ; a
# mutation that made `--version` always report "no image" PASSED it, because both
# lines then agreed. Recorded because that is the hole the oracle had to close.
probe=$(mktemp -p "$PWD" version.probe.XXXXXX.scala)
trap 'rm -f "$probe"' EXIT
printf 'println(1)\n' > "$probe"

# `set -u` is on, so an unset positional must not be expanded bare; each helper
# takes its environment as an explicit, possibly empty, list.
image_flag() {  # image_flag [VAR=VAL ...] -> 0 or 1
    env PROTOSCALA_PRELUDE_TIMING=1 "$@" "$PROTOSCALA" "$probe" 2>&1 |
        sed -n 's/.*[[:space:]]image=\([01]\).*/\1/p' | head -1
}
prelude_line() {  # prelude_line [VAR=VAL ...]
    env "$@" "$PROTOSCALA" --version 2>&1 | grep '^prelude:'
}

truth_default=$(image_flag)
truth_disabled=$(image_flag PROTOSCALA_PRELUDE_NO_IMAGE=1)
line_default=$(prelude_line)
line_disabled=$(prelude_line PROTOSCALA_PRELUDE_NO_IMAGE=1)

if [[ -z "$truth_default" ]]; then
    echo "FAIL: PROTOSCALA_PRELUDE_TIMING printed no image= flag, so this check has no oracle"
    exit 1
fi
if [[ -z "$line_default" ]]; then
    echo "FAIL: --version printed no 'prelude:' line"
    exit 1
fi

says_image() { grep -q 'precompiled image (set' <<<"$1"; }

# Default invocation: the line must agree with the flag.
if [[ "$truth_default" == "1" ]]; then
    says_image "$line_default" || {
        echo "FAIL: the runtime took the image path (image=1) but --version denies it"
        echo "  --version: $line_default"
        exit 1; }
else
    ! says_image "$line_default" || {
        echo "FAIL: the runtime compiled the source (image=0) but --version claims the image"
        echo "  --version: $line_default"
        exit 1; }
fi

# With the image disabled the runtime must take the source path, and the line must
# say which of the two source-path reasons applies.
if [[ "$truth_disabled" != "0" ]]; then
    echo "FAIL: PROTOSCALA_PRELUDE_NO_IMAGE=1 did not take the source path (image=$truth_disabled)"
    exit 1
fi
if says_image "$line_disabled"; then
    echo "FAIL: with the image disabled, --version still claims the precompiled image"
    echo "  $line_disabled"
    exit 1
fi
if [[ "$truth_default" == "1" ]]; then
    grep -q 'PROTOSCALA_PRELUDE_NO_IMAGE is set' <<<"$line_disabled" || {
        echo "FAIL: this build HAS an image, so the disabled line must say so: $line_disabled"
        exit 1; }
fi
echo OK
