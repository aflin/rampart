#!/bin/bash

# This file will be placed in the install directory and can be run from there.

if [ `whoami` == 'root' ]; then
    echo "Some tests will fail if run as root. Switch to a non-root user to run tests."
    exit 1;
fi

for i in `ls test/*-test.js`; do
	echo
	echo $i
	bin/rampart $i
	if [ "$?" != "0" ]; then
		echo "Test ${i} failed"
		exit 1;
	fi;
done

for i in `ls modules/*.js`; do
    if [ "$i" == "modules/babel.js" ] ||  [ "$i" == "modules/babel-polyfill.js" ] ; then
        continue;
    fi
    echo
	echo $i
	bin/rampart $i
	if [ "$?" != "0" ]; then
		echo "Test ${i} failed"
		exit 1;
	fi;
done
