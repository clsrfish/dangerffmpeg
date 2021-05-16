#!/bin/bash

cd $(dirname $0)

if [ ! -d build ]; then
    echo "-- Build directory not exists"
fi

program=main
if [ -f ./build/${program} ]; then
    rm ./build/${program}
fi

cmake --log-level=VERBOSE -Wdev -DCMAKE_BUILD_TYPE=Debug -S . -B build

if [ $? != 0 ]; then
    exit $?
fi

cmake --build build
if [ $? != 0 ]; then
    exit $?
fi

if [ -x ./build/${program} ]; then
    otool -L ./build/${program}
    echo "-- Launching main, arguments: ${@}"
    ./build/${program} ${@}
fi
