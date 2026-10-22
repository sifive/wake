#!/bin/sh

set -eu

WAKE="${1:+$1/wake}"
WAKE="${WAKE:-wake}"

"${WAKE}" --property test.value=command-line systemProperty test.value
"${WAKE}" systemProperty missing.value
"${WAKE}" --property test.value= systemProperty test.value
env 'test.value=environment' "${WAKE}" systemProperty test.value
