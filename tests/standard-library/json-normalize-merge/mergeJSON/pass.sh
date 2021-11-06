#! /bin/sh

ln ../json-test.wake .
"${1:-wake}" --quiet --stdout=warning,report 'jsonArrayTest mergeJSON' \
    recurse.json \
    override.json
rm json-test.wake
rm -r build
