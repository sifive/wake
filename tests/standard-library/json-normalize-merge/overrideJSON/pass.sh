#! /bin/sh

ln ../json-test.wake .
WAKE="${1:+$1/wake}"
"${WAKE:-wake}" --quiet --stdout=warning,report 'jsonArrayTest (_.overrideJSON | Pass)' \
    recurse.json \
    override.json
rm json-test.wake
rm -r build
