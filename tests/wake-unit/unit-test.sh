#! /bin/sh

set -e

TERM=xterm-256color script --return --quiet -c "$1 --no-color" /dev/null

# We run the tests *twice* so that shared caching can run without a clean slate
TERM=xterm-256color script --return --quiet -c "$1 --no-color" /dev/null > /dev/null

rm -rf job_cache_test
rm -rf .job_cache_test
rm -rf job_cache_test2
rm -rf .job_cache_test2