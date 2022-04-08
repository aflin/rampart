#!/bin/bash

# This file will be placed in the install directory and can be run from there.

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
    