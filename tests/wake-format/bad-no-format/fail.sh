#! /bin/sh

"${1}/wake-format" --no-rng bad-no-format.wake
CODE="$?"

rm bad-no-format.wake.tmp.00000000000000000000000000000000

exit $CODE