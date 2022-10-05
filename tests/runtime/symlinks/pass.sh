#! /bin/sh

set -ex

WAKE="${1:+$1/wake}"
WAKE="${WAKE:-wake}"

rm -f datFile badLink goodLink wake.db
"${WAKE}" -v test
"${WAKE}" -v test

rm wake.db
"${WAKE}" -v test

rm -f badLink goodLink
ln -s badFile badLink
ln -s datFile goodLink
"${WAKE}" -v test
"${WAKE}" -v test

rm -f datFile badLink goodLink wake.db
ln -s badFile badLink
ln -s datFile goodLink
"${WAKE}" -v test
"${WAKE}" -v test

rm -f datFile badLink goodLink .fuse.log
echo "Symlinks work!"
