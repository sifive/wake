#! /bin/sh

set -e
WAKE="${1:+$1/wake}"

echo "Cleaning up stale artifacts"

rm wake.db 2> /dev/null || true
rm -rf .cache-hit 2> /dev/null || true
rm -rf .cache-misses 2> /dev/null || true
rm -rf .job-cache 2> /dev/null || true
rm one.txt 2> /dev/null || true
rm two.txt 2> /dev/null || true
rm three.txt 2> /dev/null || true
rm four.txt 2> /dev/null || true

echo "Running test one to fill the cache"
WAKE_SHARED_CACHE_FAST_CLOSE=1 DEBUG_WAKE_SHARED_CACHE=1 WAKE_LOCAL_JOB_CACHE=.job-cache "${WAKE:-wake}" test one
rm wake.db
rm -rf .cache-misses
sleep 1

echo "Running test two to fill the cache"
WAKE_SHARED_CACHE_FAST_CLOSE=1 DEBUG_WAKE_SHARED_CACHE=1 WAKE_LOCAL_JOB_CACHE=.job-cache "${WAKE:-wake}" test two
rm wake.db
rm -rf .cache-misses
sleep 2

echo "Running test three to fill the cache"
WAKE_SHARED_CACHE_FAST_CLOSE=1 DEBUG_WAKE_SHARED_CACHE=1 WAKE_LOCAL_JOB_CACHE=.job-cache "${WAKE:-wake}" test three
rm wake.db
rm -rf .cache-misses
sleep 1

# Times:
# one: 4 of 6 old
# two: 3 of 6 old
# three: 1 of 6 old
echo "Running test one again. Should be a cache hit"
WAKE_SHARED_CACHE_FAST_CLOSE=1 DEBUG_WAKE_SHARED_CACHE=1 WAKE_LOCAL_JOB_CACHE=.job-cache "${WAKE:-wake}" test one
rm wake.db
rm -rf .cache-misses
if [ -z "$(ls -A .cache-hit)" ]; then
  echo "No cache hit found"
  exit 1
fi
sleep 3

# Times:
# one: 7 of 6 old (evicted)
# two: 6 of 6 old (evicted)
# three: 4 of 6 old

echo "Going over. Expecting one and two to be dropped"

echo "Running test one again to refill the cache"
WAKE_SHARED_CACHE_FAST_CLOSE=1 DEBUG_WAKE_SHARED_CACHE=1 WAKE_LOCAL_JOB_CACHE=.job-cache "${WAKE:-wake}" test one
rm wake.db
rm -rf .cache-misses

echo "Running test three again. Should be a cache hit"
rm -rf .cache-hit  2> /dev/null || true
WAKE_SHARED_CACHE_FAST_CLOSE=1 DEBUG_WAKE_SHARED_CACHE=1 WAKE_LOCAL_JOB_CACHE=.job-cache "${WAKE:-wake}" test three
rm wake.db
if [ -z "$(ls -A .cache-hit)" ]; then
  echo "No cache hit found"
  exit 1
fi
if [ -d ".cache-misses" ]; then
  echo "Found a cache miss"
  exit 1
fi
sleep 1

# Times:
# one: 1 of 6 old (refilled)
# two: 7 of 6 old (evicted)
# three: 5 of 6 old

echo "Running test two again. Should be a cache miss"
WAKE_SHARED_CACHE_FAST_CLOSE=1 DEBUG_WAKE_SHARED_CACHE=1 WAKE_LOCAL_JOB_CACHE=.job-cache "${WAKE:-wake}" test two
rm wake.db
if [ -z "$(ls -A .cache-misses)" ]; then
  echo "Expected a cache miss!!"
  exit 1
fi
rm -rf .cache-misses
sleep 1

# Times:
# one: 2 of 6 old (refilled)
# two: 1 of 6 old (refilled)
# three: 6 of 6 old (evicted)

echo "Running test one again. Should be a cache hit"
WAKE_SHARED_CACHE_FAST_CLOSE=1 DEBUG_WAKE_SHARED_CACHE=1 WAKE_LOCAL_JOB_CACHE=.job-cache "${WAKE:-wake}" test one
rm wake.db
if [ -d ".cache-misses" ]; then
  echo "Found a cache miss"
  exit 1
fi
rm -rf .cache-hit

echo "Running test four to fill the cache"
WAKE_SHARED_CACHE_FAST_CLOSE=1 DEBUG_WAKE_SHARED_CACHE=1 WAKE_LOCAL_JOB_CACHE=.job-cache "${WAKE:-wake}" test four
rm wake.db
if [ -z "$(ls -A .cache-misses)" ]; then
  echo "Expected a cache miss!!"
  exit 1
fi
rm -rf .cache-misses

# Times:
# one: 2 of 6 old (refilled)
# two: 1 of 6 old (refilled)
# three: 6 of 6 old (evicted)
# four: 0 of 6 old

echo "Running test two again. Should be a cache hit"
WAKE_SHARED_CACHE_FAST_CLOSE=1 DEBUG_WAKE_SHARED_CACHE=1 WAKE_LOCAL_JOB_CACHE=.job-cache "${WAKE:-wake}" test two
rm wake.db
if [ -d ".cache-misses" ]; then
  echo "Found a cache miss"
  exit 1
fi
rm -rf .cache-hit

echo "Running test four again. Should be a cache hit"
WAKE_SHARED_CACHE_FAST_CLOSE=1 DEBUG_WAKE_SHARED_CACHE=1 WAKE_LOCAL_JOB_CACHE=.job-cache "${WAKE:-wake}" test four
rm wake.db
if [ -z "$(ls -A .cache-hit)" ]; then
  echo "No cache hit found"
  exit 1
fi
if [ -d ".cache-misses" ]; then
  echo "Found a cache miss"
  exit 1
fi


# Cleanup
rm one.txt
rm two.txt
rm three.txt
rm four.txt
