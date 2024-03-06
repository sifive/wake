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
sleep 1

echo "Running test three to fill the cache"
WAKE_SHARED_CACHE_FAST_CLOSE=1 DEBUG_WAKE_SHARED_CACHE=1 WAKE_LOCAL_JOB_CACHE=.job-cache "${WAKE:-wake}" test three
rm wake.db
rm -rf .cache-misses
sleep 1

# We should still be under the limit here. This is to ensure we mark test one as used
echo "Running test one again. Should be a cache hit"
WAKE_SHARED_CACHE_FAST_CLOSE=1 DEBUG_WAKE_SHARED_CACHE=1 WAKE_LOCAL_JOB_CACHE=.job-cache "${WAKE:-wake}" test one
rm wake.db
rm -rf .cache-misses
if [ -z "$(ls -A .cache-hit)" ]; then
  echo "No cache hit found"
  exit 1
fi
sleep 4

# Now we're going to go over. Hopefully dropping two and three, but keeping one and four
echo "Going over. Expecting two and three to be dropped"
echo "Running test one again. Should be a cache hit"
WAKE_SHARED_CACHE_FAST_CLOSE=1 DEBUG_WAKE_SHARED_CACHE=1 WAKE_LOCAL_JOB_CACHE=.job-cache "${WAKE:-wake}" test one
rm wake.db
rm -rf .cache-misses

# Now make sure we still get a hit on 1
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


# And check that we get misses on four and three
echo "Running test two again. Should be a cache miss"
WAKE_SHARED_CACHE_FAST_CLOSE=1 DEBUG_WAKE_SHARED_CACHE=1 WAKE_LOCAL_JOB_CACHE=.job-cache "${WAKE:-wake}" test two
rm wake.db
if [ -z "$(ls -A .cache-misses)" ]; then
  echo "Expected a cache miss!!"
  exit 1
fi
rm -rf .cache-misses
sleep 1


echo "Running test one again. Should be a cache hit"
WAKE_SHARED_CACHE_FAST_CLOSE=1 DEBUG_WAKE_SHARED_CACHE=1 WAKE_LOCAL_JOB_CACHE=.job-cache "${WAKE:-wake}" test one
rm wake.db
if [ -d ".cache-misses" ]; then
  echo "Found a cache miss"
  exit 1
fi
rm -rf .cache-hit

echo "Running test four again. Should be a cache miss"
WAKE_SHARED_CACHE_FAST_CLOSE=1 DEBUG_WAKE_SHARED_CACHE=1 WAKE_LOCAL_JOB_CACHE=.job-cache "${WAKE:-wake}" test four
rm wake.db
if [ -z "$(ls -A .cache-misses)" ]; then
  echo "Expected a cache miss!!"
  exit 1
fi
rm -rf .cache-misses


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
