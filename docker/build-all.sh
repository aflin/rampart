#!/bin/bash

die() {
    echo "Build/Install for $1 failed"
    exit 1;
}

for i in 2014 2_28; do
    ./build.sh -b $i build || die $i;
    ./build.sh -b $i install --yes || die $i;
done;
