#! /bin/sh

ln ../json-test.wake .
"${1:-wake}" --quiet --stdout=warning,report 'jsonTest normalizeJSONCompat' input.json
rm json-test.wake
rm -r build
