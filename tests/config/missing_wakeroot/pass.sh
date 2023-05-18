#! /bin/sh

if [ $(uname) != Linux ] ; then
  cat stdout
  exit 0
fi

WAKE="${1:+$1/wake}"
HOME=/home "${WAKE:-wake}" --config
