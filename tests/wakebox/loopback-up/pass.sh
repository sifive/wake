#!/usr/bin/env bash
# It's not valid to call wakebox with an empty PATH.
# So we fill PATH with some typical values.
export PATH=/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin

# capture the output, and then print it out on error

STDOUT=$(${1}/wakebox -p input.json)
RET="$?"

if [[ $RET -ne 0 ]] || [[ ! $(echo "$STDOUT" | grep -o UP | head -1) = "UP" ]]; then
    echo $STDOUT 1>&2
    exit 1
fi
