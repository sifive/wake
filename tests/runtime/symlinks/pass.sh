#! /bin/sh

set -ex

echo "shex 1"
WAKE="${1:+$1/wake}"
echo "shex 2"
WAKE="${WAKE:-wake}"
echo "shex 3"

rm -f datFile badLink goodLink wake.db
echo "shex 4"
DEBUGNAME=wake.test.symlink1.log "${WAKE}" -v test
echo "shex 5"
DEBUGNAME=wake.test.symlink2.log "${WAKE}" -v test
echo "shex 6"

rm wake.db
echo "shex 7"
DEBUGNAME=wake.test.symlink3.log "${WAKE}" -v test
echo "shex 8"

rm -f badLink goodLink
echo "shex 9"
ln -s badFile badLink
echo "shex 10"
ln -s datFile goodLink
echo "shex 11"
DEBUGNAME=wake.test.symlink4.log "${WAKE}" -v test
echo "shex 12"
DEBUGNAME=wake.test.symlink5.log "${WAKE}" -v test
echo "shex 13"

rm -f datFile badLink goodLink wake.db
echo "shex 14"
ln -s badFile badLink
echo "shex 15"
ln -s datFile goodLink
echo "shex 16"
DEBUGNAME=wake.test.symlink6.log "${WAKE}" -v test
echo "shex 17"
DEBUGNAME=wake.test.symlink7.log "${WAKE}" -v test
echo "shex 18"

rm -f datFile badLink goodLink .fuse.log
echo "shex 19"
echo "Symlinks work!"
