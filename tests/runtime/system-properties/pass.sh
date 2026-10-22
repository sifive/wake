#!/bin/sh

set -eu

WAKE="${1:+$1/wake}"
WAKE="${WAKE:-wake}"

"${WAKE}" --property test.value=command-line systemProperty
"${WAKE}" --property test.value=command-line missingSystemProperty
"${WAKE}" --property test.value=command-line environmentProperty
"${WAKE}" --property test.value=command-line primitiveSystemPropertyPresent
"${WAKE}" primitiveSystemPropertyMissing
"${WAKE}" --property test.value= primitiveSystemPropertyEmpty
"${WAKE}" --property test.value= emptySystemProperty
