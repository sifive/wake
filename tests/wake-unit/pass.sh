#! /bin/sh

set -e

TERM=xterm-256color script --return --quiet -c "$2 --no-color" /dev/null
