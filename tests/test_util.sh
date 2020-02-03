#!/usr/bin/env bash

test_root=$(dirname $(perl -MCwd -e "print Cwd::realpath('$0')"))
wake_root=$(perl -MCwd -e "print Cwd::realpath('$test_root/..')")

fail=0
pass=0
in_prereq=0

check() {
    check_name=$1
    shift;

    if $@
    then echo "PASS - ${check_name}"; pass=$((pass+1))
    else echo "FAIL - ${check_name}"; fail=$((fail+1))
    fi
}

prereq() {
    if [ "$1" = "off" ]
    then in_prereq=0; set +e -x
    else in_prereq=1; set -e +x
    fi
}

report() {
    set +x
    echo "PASS: $pass"
    echo "FAIL: $fail"
}

finish() {
    if [ $in_prereq -eq 1 ]
    then fail=$((fail+1))
    fi

    if [ $pass -eq 0 ] && [ $fail -eq 0 ]
    then fail=$((fail+1))
    fi

    if [ $fail -eq 0 ]
    then echo "Test passed"; exit 0
    else echo "Test failed"; exit 1
    fi
}

compile_wake() {(
    cd $wake_root                     &&
    make clean                        &&
    mkdir -p "${1}/build"             &&
    make install DESTDIR="${1}/build"
)}
