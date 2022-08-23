#! /bin/sh

"${1}/wake-format" --no-rng leading-comment-indent.wake
CODE="$?"

rm leading-comment-indent.wake.tmp.00000000000000000000000000000000

exit $CODE