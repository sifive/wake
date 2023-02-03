#! /bin/sh

set -e

TERM=xterm-256color script --return --quiet -c "$1 --no-color" /dev/null

