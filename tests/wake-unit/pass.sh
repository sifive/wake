#! /bin/sh

set -e

# "${1}/wake-format" basic.wake > "$tmp"
WAKE_UNIT="${1}/wake-unit"

TERM=xterm-256color script --return --quiet -c "$WAKE_UNIT --no-color --tag threaded" /dev/null

# We run the tests *twice* so that shared caching can run without a clean slate
TERM=xterm-256color script --return --quiet -c "$WAKE_UNIT --no-color --tag threaded" /dev/null > /dev/null

rm -rf job_cache_test
rm -rf .job_cache_test
rm -rf job_cache_test2
rm -rf .job_cache_test2
