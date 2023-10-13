#!/bin/bash

set -x

DEST="$1"
VERSION=0.5.0

# Create a temp build dir
TMPDIR=$(mktemp -d)
cd $TMPDIR

# Remove temp build dir on error or exit.
function cleanup () { rm -rf $TMPDIR; }
trap cleanup EXIT

wget https://github.com/vasi/squashfuse/releases/download/v$VERSION/squashfuse-$VERSION.tar.gz
tar xzf squashfuse-$VERSION.tar.gz

TMP_INSTALL_DEST=$PWD/tmp_install_dest
mkdir $TMP_INSTALL_DEST

# Build and configure
cd squashfuse-$VERSION

./configure \
    --disable-demo \
    --disable-high-level \
    --prefix=$TMP_INSTALL_DEST \
    --program-prefix=wake_

# Make and Install
make -j $(nproc) install

# Copy Binary
cp $TMP_INSTALL_DEST/bin/wake_squashfuse_ll $DEST/bin
# Copy License
mkdir -p $DEST/licenses
cp ./LICENSE $DEST/licenses
