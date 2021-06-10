#! /bin/sh

set -ex

rm -f datFile badLink goodLink wake.db
"${1:-wake}" -v test
"${1:-wake}" -v test

rm wake.db
"${1:-wake}" -v test

rm -f datFile badLink goodLink
ln -s whatever datFile
ln -s whatever badLink
ln -s whatever goodLink
"${1:-wake}" -v test
"${1:-wake}" -v test

rm -f datFile badLink goodLink wake.db
ln -s whatever datFile
ln -s whatever badLink
ln -s whatever goodLink
"${1:-wake}" -v test
"${1:-wake}" -v test

echo "Symlinks work!"
