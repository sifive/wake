#!/bin/sh
# It's not valid to call wakebox with an empty PATH.
# So we fill PATH with some typical values.
export PATH=/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin


${1}/wakebox -I -p input.json -o result.json
RET=$?

[ $RET -ne 0 ] && exit 99

[ $(cat result.json | jq .usage.status) -ne 97 ] && exit 98

exit 0
