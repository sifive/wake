#! /bin/sh

ln ../json-test.wake .
WAKE="${1:+$1/wake}"
"${WAKE:-wake}" --quiet --stdout=warning,report 'jsonTest normalizeJSONCompat' input.json
rm json-test.wake
rm -r build
