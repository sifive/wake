#! /bin/sh

# Leading comment indent test
if "${1}/wake-format" --no-rng leading-comment-indent.wake; then
  rm leading-comment-indent.wake.tmp.00000000000000000000000000000000
  exit 0
fi

# Trailing comment test
if "${1}/wake-format" --no-rng trailing-comment.wake; then
  rm leading-comment-indent.wake.tmp.00000000000000000000000000000000
  rm trailing-comment.wake.tmp.00000000000000000000000000000000
  exit 0
fi

rm leading-comment-indent.wake.tmp.00000000000000000000000000000000
rm trailing-comment.wake.tmp.00000000000000000000000000000000
exit 1
