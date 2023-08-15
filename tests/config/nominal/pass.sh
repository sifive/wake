#! /bin/sh

if [ $(uname) != Linux ] ; then
  cat stdout
  exit 0
fi

WAKE="${1:+$1/wake}"
WAKE_SHARED_CACHE_MAX_SIZE=1024 "${WAKE:-wake}" --config
