#! /bin/sh
WAKE="${1:+$1/wake}"
"${WAKE:-wake}" -qv -x Unit
