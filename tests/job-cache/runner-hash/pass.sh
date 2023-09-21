#! /bin/sh

if [ $(uname) != Linux ] ; then
  exit 0
fi

set -e
WAKE="${1:+$1/wake}"

rm test.txt || true
rm wake.db || true
rm -rf .cache-hit || true
rm -rf .cache-misses || true
rm -rf .job-cache || true
WAKE_SHARED_CACHE_FAST_CLOSE=1 DEBUG_WAKE_SHARED_CACHE=1 WAKE_LOCAL_JOB_CACHE=.job-cache "${WAKE:-wake}" test

rm wake.db
rm -rf .cache-misses
WAKE_SHARED_CACHE_FAST_CLOSE=1 DEBUG_WAKE_SHARED_CACHE=1 WAKE_LOCAL_JOB_CACHE=.job-cache "${WAKE:-wake}" test


if [ -z "$(ls -A .cache-misses)" ]; then
  echo "Expected a cache miss!!"
  exit 1
fi

if [ -d ".cache-hit" ]; then
  echo "Found an unexpected cache hit"
  exit 1
fi

# Verify correct bits
rm test_gold.txt || true
cat some-input.txt > test_gold.txt && cat some-input.txt >> test_gold.txt
diff test.txt test_gold.txt >&2

# Cleanup
echo "fourth round" >&2
rm test.txt
rm test_gold.txt
