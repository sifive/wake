#! /bin/sh

ln ../json-test.wake .
WAKE="${1:+$1/wake}"
"${WAKE:-wake}" --quiet --stdout=warning,report 'jsonTest normalizeJSONIdentity' \
    infinity.json \
    nan.json \
    unicode.json
rm json-test.wake
rm -r build
