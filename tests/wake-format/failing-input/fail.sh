#! /bin/bash

function cleanup {
  rm leading-comment-indent.wake.tmp.00000000000000000000000000000000
  rm trailing-comment.wake.tmp.00000000000000000000000000000000
}
trap cleanup EXIT

# Leading comment indent test
if "${1}/wake-format" --no-rng leading-comment-indent.wake; then
  exit 0
fi

# Trailing comment test
if "${1}/wake-format" --no-rng trailing-comment.wake; then
  exit 0
fi

exit 1
