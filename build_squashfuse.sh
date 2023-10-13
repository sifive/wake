#!/bin/bash

set -x

DEST="$1"
VERSION=0.5.0

wget https://github.com/vasi/squashfuse/releases/download/v$VERSION/squashfuse-$VERSION.tar.gz
tar xzf squashfuse-$VERSION.tar.gz

# Build and configure
cd squashfuse-$VERSION
./configure --prefix=$DEST --program-prefix=wake_


# Make and Install
make -j $(nproc) install
