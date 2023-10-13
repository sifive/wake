#!/bin/sh
#
# It's not valid to call wakebox with an empty PATH.
# So we fill PATH with some typical values.
export PATH=/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin

${1}/wakebox -p input.json -o result.json

trap "rm result.json" EXIT

[ "$(cat result.json | jq .timed_out)" = "true" ] && exit 0

exit 1
