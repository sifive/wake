#! /bin/sh

set -e
WAKE="${1:+$1/wake}"

cleanup () {
  rm -f wake.db 2> /dev/null || true
  rm -rf .cache-misses  2> /dev/null || true
  rm -rf .cache-hit  2> /dev/null || true
}

expect_hit() {
  if [ -z "$(ls -A .cache-hit)" ]; then
    echo "Expected a cache hit but none found"
    exit 1
  fi
}

expect_miss() {
  if [ -z "$(ls -A .cache-misses)" ]; then
    echo "Expected a cache miss but none found"
    exit 1
  fi
}

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
cleanup
sleep 1

echo "Running test two to fill the cache"
WAKE_SHARED_CACHE_FAST_CLOSE=1 DEBUG_WAKE_SHARED_CACHE=1 WAKE_LOCAL_JOB_CACHE=.job-cache "${WAKE:-wake}" test two
cleanup
sleep 2

echo "Running test three to fill the cache"
WAKE_SHARED_CACHE_FAST_CLOSE=1 DEBUG_WAKE_SHARED_CACHE=1 WAKE_LOCAL_JOB_CACHE=.job-cache "${WAKE:-wake}" test three
cleanup
sleep 1

# Times:
# one: 4 of 6 old
# two: 3 of 6 old
# three: 1 of 6 old
echo "Running test one again. Should be a cache hit"
WAKE_SHARED_CACHE_FAST_CLOSE=1 DEBUG_WAKE_SHARED_CACHE=1 WAKE_LOCAL_JOB_CACHE=.job-cache "${WAKE:-wake}" test one
expect_hit
cleanup
sleep 3

# Times:
# one: 7 of 6 old (evicted)
# two: 6 of 6 old (evicted)
# three: 4 of 6 old

echo "Going over. Expecting one and two to be dropped"

echo "Running test one again to refill the cache"
WAKE_SHARED_CACHE_FAST_CLOSE=1 DEBUG_WAKE_SHARED_CACHE=1 WAKE_LOCAL_JOB_CACHE=.job-cache "${WAKE:-wake}" test one
cleanup

echo "Running test three again. Should be a cache hit"
WAKE_SHARED_CACHE_FAST_CLOSE=1 DEBUG_WAKE_SHARED_CACHE=1 WAKE_LOCAL_JOB_CACHE=.job-cache "${WAKE:-wake}" test three
expect_hit
cleanup
sleep 1

# Times:
# one: 1 of 6 old (refilled)
# two: 7 of 6 old (evicted)
# three: 5 of 6 old

echo "Running test two again. Should be a cache miss"
WAKE_SHARED_CACHE_FAST_CLOSE=1 DEBUG_WAKE_SHARED_CACHE=1 WAKE_LOCAL_JOB_CACHE=.job-cache "${WAKE:-wake}" test two
rm wake.db
expect_miss
cleanup
sleep 1

# Times:
# one: 2 of 6 old (refilled)
# two: 1 of 6 old (refilled)
# three: 6 of 6 old (evicted)

echo "Running test one again. Should be a cache hit"
WAKE_SHARED_CACHE_FAST_CLOSE=1 DEBUG_WAKE_SHARED_CACHE=1 WAKE_LOCAL_JOB_CACHE=.job-cache "${WAKE:-wake}" test one
expect_hit
cleanup

echo "Running test four to fill the cache"
WAKE_SHARED_CACHE_FAST_CLOSE=1 DEBUG_WAKE_SHARED_CACHE=1 WAKE_LOCAL_JOB_CACHE=.job-cache "${WAKE:-wake}" test four
expect_miss
cleanup

# Times:
# one: 2 of 6 old (refilled)
# two: 1 of 6 old (refilled)
# three: 6 of 6 old (evicted)
# four: 0 of 6 old

echo "Running test two again. Should be a cache hit"
WAKE_SHARED_CACHE_FAST_CLOSE=1 DEBUG_WAKE_SHARED_CACHE=1 WAKE_LOCAL_JOB_CACHE=.job-cache "${WAKE:-wake}" test two
expect_hit
cleanup

echo "Running test four again. Should be a cache hit"
WAKE_SHARED_CACHE_FAST_CLOSE=1 DEBUG_WAKE_SHARED_CACHE=1 WAKE_LOCAL_JOB_CACHE=.job-cache "${WAKE:-wake}" test four
expect_hit
cleanup


# Cleanup job files 
rm one.txt
rm two.txt
rm three.txt
rm four.txt
