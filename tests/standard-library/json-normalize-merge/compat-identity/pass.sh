#! /bin/sh

ln ../json-test.wake .
"${1:-wake}" --quiet --stdout=warning,report 'jsonTest normalizeJSONIdentity' \
    infinity.json \
    nan.json \
    unicode.json
rm json-test.wake
rm -r build
