#! /bin/sh

if [ $(uname) != Linux ] ; then
  cat stdout
  cat stderr 1>&2
  exit 0
fi

WAKE="${1:+$1/wake}"
"${WAKE:-wake}" --config
