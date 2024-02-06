#!/bin/sh
# It's not valid to call wakebox with an empty PATH.
# So we fill PATH with some typical values.
export PATH=/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin

trap 'rm link_file_src.txt link_file_dst.txt' EXIT

STDOUT=$(${1}/wakebox -p input.json)

[ "$STDOUT" = "" ] || exit 1
