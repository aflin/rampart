#!/bin/bash

# This file will be placed in the install directory and can be run from there.

for i in `ls test/*-test.js`; do
	bin/rampart $i
	if [ "$?" != "0" ]; then
		echo "Test ${i} failed"
		exit 1;
	fi;
done
