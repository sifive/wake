#! /bin/sh

# Trailing comment test
"${1}/wake-format" --no-rng trailing-comment.wake
CODE="$?"

rm trailing-comment.wake.tmp.00000000000000000000000000000000

exit $CODE
