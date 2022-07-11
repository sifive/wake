#! /bin/sh

WAKE="${1:+$1/wake}"
"${WAKE:-wake}" --in foo -q test
