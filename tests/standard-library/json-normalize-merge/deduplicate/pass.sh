#! /bin/sh

ln ../json-test.wake .
WAKE="${1:+$1/wake}"
"${WAKE:-wake}" --quiet --stdout=warning,report 'jsonTest normalizeJSONCompat' \
    valid.json \
    valid-nesting.json \
    invalid-boolean.json \
    invalid-double.json \
    invalid-int.json \
    invalid-string.json \
    invalid-nesting.json
rm json-test.wake
rm -r build
