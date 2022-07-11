#! /bin/sh

set -ex

WAKE="${1:+$1/wake}"
WAKE="${WAKE:-wake}"

rm -f datFile badLink goodLink wake.db
"${WAKE}" -v test
"${WAKE}" -v test

rm wake.db
"${WAKE}" -v test

rm -f datFile badLink goodLink
ln -s whatever datFile
ln -s whatever badLink
ln -s whatever goodLink
"${WAKE}" -v test
"${WAKE}" -v test

rm -f datFile badLink goodLink wake.db
ln -s whatever datFile
ln -s whatever badLink
ln -s whatever goodLink
"${WAKE}" -v test
"${WAKE}" -v test

rm -f datFile badLink goodLink .fuse.log
echo "Symlinks work!"
