#! /bin/sh

# If not on Linux we just don't test
if [ $(uname) != Linux ] ; then
  exit 0
fi

# Setup our initital state to make sure we're clean
set -e
WAKE="${1:+$1/wake}"
rm wake.db || true
rm -rf .cache-hit || true
rm -rf .cache-misses || true
rm -rf .job-cache || true

# Now run again with a small payload, expect a miss
WAKE_SHARED_CACHE_FAST_CLOSE=1 PAYLOAD=small DEBUG_WAKE_SHARED_CACHE=1 WAKE_EXPERIMENTAL_JOB_CACHE=.job-cache "${WAKE:-wake}" test
rm wake.db
rm -rf .cache-misses

# Run test once with a larger payload, expect a miss
WAKE_SHARED_CACHE_FAST_CLOSE=1 PAYLOAD=deadbeefdeadbeef DEBUG_WAKE_SHARED_CACHE=1 WAKE_EXPERIMENTAL_JOB_CACHE=.job-cache "${WAKE:-wake}" test
rm wake.db
rm -rf .cache-misses

# Now run again with a small payload again, this should be a cache hit but it should
# not overwrite the existing inode, it should create a new one.
WAKE_SHARED_CACHE_FAST_CLOSE=1 PAYLOAD=small DEBUG_WAKE_SHARED_CACHE=1 WAKE_EXPERIMENTAL_JOB_CACHE=.job-cache "${WAKE:-wake}" test
if [ -z "$(ls -A .cache-hit)" ]; then
  echo "No cache hit found"
  exit 1
fi
if [ -d ".cache-misses" ]; then
  echo "Found a cache miss"
  exit 1
fi

# Verify
echo small | diff - test.txt

# Cleanup
rm -rf ./bar
rm test.txt
rm wake.db
