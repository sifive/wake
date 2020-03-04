#!/usr/bin/env bash

. $(dirname $0)/test_util.sh

timestamp=`date +'%Y-%m-%dT%H-%M-%S'`

test_dir="$(mktemp -d)/test.${timestamp}"
echo "Running tests in ${test_dir}"
mkdir $test_dir

echo Compiling wake
set -e
compile_wake $test_dir
wake_dir="${test_dir}/build/bin"
export PATH=$wake_dir:$PATH
echo Using wake executable at $(which wake)
set +e

declare -A test_results
pass=0
fail=0
for test_path in $test_root/*.t; do
    cd $test_dir
    test_file=$(basename $test_path)
    test_name="${test_file%%.*}"

    echo "Running test [$test_name]"
    mkdir $test_name
    cd $test_name

    $test_path
    if [ $? -eq 0 ]; then
        test_results["$test_name"]="PASS"
        touch "PASS"
        ((pass++))
    else
        test_results["$test_name"]="FAIL"
        touch "FAIL"
        ((fail++))
        test_result=1
    fi
    echo -e "\033[1K\033[1G${test_results[$test_name]} - ${test_name}";
done

echo
echo
echo "Results:"
echo "Passing: $pass"
echo "Failing: $fail"

if [ $fail -ne 0 ]
then
    echo "Not removing test dir $test_dir"
    exit 1
else
    echo "Removing test dir $test_dir"
    rm -rf $test_dir
    exit 0
fi
