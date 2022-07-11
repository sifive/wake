#! /bin/sh

ln ../json-test.wake .
WAKE="${1:+$1/wake}"
"${WAKE:-wake}" --quiet --stdout=warning,report 'jsonTest normalizeJSONCompat' \
    infinity.json \
    nan.json \
    unicode.json \
    unicode-merge.json
rm json-test.wake
rm -r build
