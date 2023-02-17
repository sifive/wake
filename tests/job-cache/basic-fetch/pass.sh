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
DEBUG_WAKE_SHARED_CACHE=1 WAKE_EXPERIMENTAL_JOB_CACHE=.job-cache "${WAKE:-wake}" test

rm wake.db
rm -rf .cache-misses
DEBUG_WAKE_SHARED_CACHE=1 WAKE_EXPERIMENTAL_JOB_CACHE=.job-cache "${WAKE:-wake}" test
if [ -z "$(ls -A .cache-hit)" ]; then
  echo "No cache hit found"
  exit 1
fi
if [ -d ".cache-misses" ]; then
  echo "Found a cache miss"
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
