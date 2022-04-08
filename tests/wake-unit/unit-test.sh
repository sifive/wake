#! /bin/sh

set -e

# TODO: Change this command to something where `isatty` is true. For now
# its fine if we don't worry about that
#TERM=xterm-256color script --return --quiet -c "$1 --no-color" /dev/null
$1
