#! /bin/sh

ln ../json-test.wake .
"${1:-wake}" --quiet --stdout=warning,report 'jsonArrayTest (_.overrideJSON | Pass)' \
    recurse.json \
    override.json
rm json-test.wake
rm -r build
