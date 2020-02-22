#!/usr/bin/env bash
. $(dirname $0)/test_util.sh

prereq on

# Make a repository with a wake file
mkdir repo1
(
  cd repo1
  git init
  echo 'global def something = "some text"' > test.wake
  git add test.wake
  git commit -m "test wake file"
)

# Create a second checkout of repo1
mkdir other_dir
(
  cd other_dir
  git clone ../repo1
)

wake --init .

prereq off

msg=$(wake)
check "Wake should raise error for repeated symbol" [ $? -ne 0 ]

echo "repo1/**" > other_dir/.wakeignore
msg=$(wake)
check "Wake should ignore repeated symbol" [ $? -eq 0 ]


report
finish
